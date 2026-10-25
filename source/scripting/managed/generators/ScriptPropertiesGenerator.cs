using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.Globalization;
using System.Linq;
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.Text;

namespace Vultra.Scripting.Generators;

[Generator]
public sealed class ScriptPropertiesGenerator : IIncrementalGenerator
{
    private static readonly DiagnosticDescriptor PartialRequired = Error("VSG001", "Script classes must be partial",
        "Script class and enclosing types must be partial: {0}");
    private static readonly DiagnosticDescriptor UnsupportedClass = Error("VSG002", "Unsupported script class",
        "Script class must not be generic or file-local: {0}");
    private static readonly DiagnosticDescriptor WritableRequired = Error("VSG003", "Export must be writable",
        "Export requires a named writable instance field or get/set property: {0}");
    private static readonly DiagnosticDescriptor UnsupportedType = Error("VSG004", "Unsupported export type",
        "Unsupported export type for {0}: {1}");
    private static readonly DiagnosticDescriptor InvalidEnum = Error("VSG005", "Unsupported export enum",
        "Exported enum must declare choices representable as signed 64-bit values: {0}");
    private static readonly DiagnosticDescriptor DuplicateName = Error("VSG006", "Duplicate exported name",
        "Exported name is already declared in this script hierarchy: {0}");
    private static readonly DiagnosticDescriptor InvalidNullable = Error("VSG007", "Invalid export nullability",
        "Nullable=false is valid only for Node/Resource reference exports: {0}");
    private static readonly DiagnosticDescriptor MissingBaseMetadata = Error("VSG008", "Base script metadata is missing",
        "Base script assembly must use Vultra.Scripting.props: {0}");
    private static readonly DiagnosticDescriptor InvalidReferenceClass = Error("VSG009", "Invalid script class constraint",
        "Explicit script class constraints require an SDK Node reference and a nonempty class name: {0}");

    private sealed record Property(ISymbol Symbol, ITypeSymbol Type, string Kind, int Bits, string? ElementKind,
                                   string? ReferenceType, string? ReferenceClass, string? ReferenceModule, bool Resource, bool Nullable,
                                   ImmutableArray<(string Label, long Value)> Choices);

    public void Initialize(IncrementalGeneratorInitializationContext context)
    {
        var classes = context.SyntaxProvider.CreateSyntaxProvider(
            static (node, _) => node is ClassDeclarationSyntax,
            static (syntax, token) => syntax.SemanticModel.GetDeclaredSymbol((ClassDeclarationSyntax)syntax.Node, token))
            .Where(static symbol => symbol != null).Collect();
        context.RegisterSourceOutput(classes, static (output, symbols) => Generate(output, symbols));
    }

    private static DiagnosticDescriptor Error(string id, string title, string message) =>
        new(id, title, message, "Vultra.Scripting", DiagnosticSeverity.Error, isEnabledByDefault: true);

    private static bool IsSdkType(ITypeSymbol? type, string name) =>
        type?.ContainingAssembly.Name == "Vultra.Scripting" && type.ContainingType == null && type.Name == name &&
        type.ContainingNamespace.ToDisplayString() == "Vultra.Scripting";

    private static INamedTypeSymbol? NativeBase(INamedTypeSymbol type)
    {
        INamedTypeSymbol? native = null;
        for (var current = type.BaseType; current != null; current = current.BaseType)
        {
            if (current.ContainingAssembly.Name == "Vultra.Scripting")
            {
                native ??= current;
            }
            if (IsSdkType(current, "Node"))
            {
                return native;
            }
        }
        return null;
    }

    private static string ClassName(INamedTypeSymbol type)
    {
        var names = new Stack<string>();
        for (var current = type; current != null; current = current.ContainingType)
        {
            names.Push(current.MetadataName);
        }
        var prefix = type.ContainingNamespace.IsGlobalNamespace ? "" : type.ContainingNamespace.ToDisplayString() + ".";
        return prefix + string.Join("+", names);
    }

    private static AttributeData? Export(ISymbol member) => member.GetAttributes()
        .FirstOrDefault(attribute => IsSdkType(attribute.AttributeClass, "ExportAttribute"));

    private static void Generate(SourceProductionContext output, ImmutableArray<INamedTypeSymbol?> candidates)
    {
        // A partial class appears once per declaration in the syntax stream, but owns one generated descriptor.
        var types = new HashSet<INamedTypeSymbol>(SymbolEqualityComparer.Default);
        foreach (var type in candidates)
        {
            if (type != null && NativeBase(type) != null)
            {
                types.Add(type);
            }
        }
        foreach (var type in types.OrderBy(ClassName, StringComparer.Ordinal))
        {
            if (!ValidateClass(output, type))
            {
                continue;
            }
            var properties = new List<Property>();
            var names = new HashSet<string>(StringComparer.Ordinal);
            var valid = true;
            for (var current = type; current != null && current.ContainingAssembly.Name != "Vultra.Scripting";
                 current = current.BaseType)
            {
                foreach (var member in current.GetMembers().OrderBy(member => member.Name, StringComparer.Ordinal))
                {
                    var attribute = Export(member);
                    if (attribute == null)
                    {
                        continue;
                    }
                    if (!names.Add(member.Name))
                    {
                        output.ReportDiagnostic(Diagnostic.Create(DuplicateName, member.Locations.FirstOrDefault(), member.Name));
                        valid = false;
                    }
                    // Inherited accessors are generated in their declaring partial class, including private members.
                    if (SymbolEqualityComparer.Default.Equals(current, type))
                    {
                        var property = Describe(output, member, attribute);
                        valid &= property != null;
                        if (property != null)
                        {
                            properties.Add(property);
                        }
                    }
                }
            }
            if (valid)
            {
                output.AddSource(ClassName(type) + ".ScriptProperties.g.cs", SourceText.From(Render(type, properties), Encoding.UTF8));
            }
        }
    }

    private static bool ValidateClass(SourceProductionContext output, INamedTypeSymbol type)
    {
        if (type.BaseType is { } baseType && baseType.ContainingAssembly.Name != "Vultra.Scripting" &&
            baseType.DeclaringSyntaxReferences.IsEmpty && !baseType.GetMembers("GetScriptClass").Any())
        {
            output.ReportDiagnostic(Diagnostic.Create(MissingBaseMetadata, type.Locations.FirstOrDefault(), baseType.Name));
            return false;
        }
        for (var current = type; current != null; current = current.ContainingType)
        {
            foreach (var declaration in current.DeclaringSyntaxReferences)
            {
                var syntax = (TypeDeclarationSyntax)declaration.GetSyntax(output.CancellationToken);
                if (!syntax.Modifiers.Any(SyntaxKind.PartialKeyword))
                {
                    output.ReportDiagnostic(Diagnostic.Create(PartialRequired, syntax.Identifier.GetLocation(), current.Name));
                    return false;
                }
                if (current.IsGenericType || syntax.Modifiers.Any(SyntaxKind.FileKeyword))
                {
                    output.ReportDiagnostic(Diagnostic.Create(UnsupportedClass, syntax.Identifier.GetLocation(), current.Name));
                    return false;
                }
            }
        }
        return true;
    }

    private static bool HasCallback(INamedTypeSymbol type, string name)
    {
        for (var current = type; current != null && current.ContainingAssembly.Name != "Vultra.Scripting";
             current = current.BaseType)
        {
            if (current.GetMembers(name).OfType<IMethodSymbol>().Any(method => method.IsOverride))
            {
                return true;
            }
        }
        return false;
    }

    private static Property? Describe(SourceProductionContext output, ISymbol member, AttributeData attribute)
    {
        ITypeSymbol? type = member switch
        {
            IFieldSymbol field when !field.IsReadOnly && !field.IsConst && !field.IsImplicitlyDeclared => field.Type,
            IPropertySymbol property when property.GetMethod != null && property.SetMethod != null &&
                                          !property.SetMethod.IsInitOnly && !property.IsIndexer => property.Type,
            _ => null
        };
        if (type == null || member.IsStatic || !member.CanBeReferencedByName)
        {
            output.ReportDiagnostic(Diagnostic.Create(WritableRequired, member.Locations.FirstOrDefault(), member.Name));
            return null;
        }
        var nullable = !attribute.NamedArguments.Any(pair => pair.Key == "Nullable" && pair.Value.Value is false);
        var scalar = type;
        var array = type is IArrayTypeSymbol;
        if (type is IArrayTypeSymbol arrayType)
        {
            if (!arrayType.IsSZArray || arrayType.ElementType is IArrayTypeSymbol)
            {
                return RejectType();
            }
            scalar = arrayType.ElementType;
        }
        var (kind, bits) = ScalarKind(scalar);
        if (kind == null)
        {
            return RejectType();
        }
        var resource = false;
        string? reference = null;
        var referenceClass = attribute.NamedArguments.FirstOrDefault(pair => pair.Key == "ReferenceClass").Value.Value as string;
        var referenceModule = attribute.NamedArguments.FirstOrDefault(pair => pair.Key == "ReferenceModule").Value.Value as string;
        if (kind == "reference")
        {
            for (var current = scalar as INamedTypeSymbol; current != null; current = current.BaseType)
            {
                resource |= IsSdkType(current, "Resource");
            }
            if (scalar.ContainingAssembly.Name != "Vultra.Scripting")
            {
                if (resource || scalar is not INamedTypeSymbol scriptType || scriptType.IsGenericType)
                {
                    return RejectType();
                }
                if (referenceClass != null || referenceModule != null)
                {
                    return RejectConstraint();
                }
                reference = NativeBase(scriptType)!.Name;
                referenceClass = ClassName(scriptType);
            }
            else
            {
                reference = IsSdkType(scalar, "Material") ? "MaterialResource" : scalar.Name;
                if ((referenceClass != null && (resource || referenceClass.Length == 0)) ||
                    (referenceModule != null && referenceClass == null))
                {
                    return RejectConstraint();
                }
            }
        }
        else if (referenceClass != null || referenceModule != null)
        {
            return RejectConstraint();
        }
        else if (!nullable)
        {
            output.ReportDiagnostic(Diagnostic.Create(InvalidNullable, member.Locations.FirstOrDefault(), member.Name));
            return null;
        }
        var choices = ImmutableArray.CreateBuilder<(string Label, long Value)>();
        if (scalar.TypeKind == TypeKind.Enum)
        {
            foreach (var choice in ((INamedTypeSymbol)scalar).GetMembers().OfType<IFieldSymbol>().Where(field => field.HasConstantValue))
            {
                try
                {
                    choices.Add((choice.Name, Convert.ToInt64(choice.ConstantValue, CultureInfo.InvariantCulture)));
                }
                catch (OverflowException)
                {
                    output.ReportDiagnostic(Diagnostic.Create(InvalidEnum, member.Locations.FirstOrDefault(), member.Name));
                    return null;
                }
            }
            if (choices.Count == 0)
            {
                output.ReportDiagnostic(Diagnostic.Create(InvalidEnum, member.Locations.FirstOrDefault(), member.Name));
                return null;
            }
        }
        return new(member, type, array ? "array" : kind, bits, array ? kind : null, reference, referenceClass, referenceModule, resource, nullable,
                   choices.OrderBy(choice => choice.Value).ThenBy(choice => choice.Label, StringComparer.Ordinal).ToImmutableArray());

        Property? RejectType()
        {
            output.ReportDiagnostic(Diagnostic.Create(UnsupportedType, member.Locations.FirstOrDefault(), member.Name, type.ToDisplayString()));
            return null;
        }

        Property? RejectConstraint()
        {
            output.ReportDiagnostic(Diagnostic.Create(InvalidReferenceClass, member.Locations.FirstOrDefault(), member.Name));
            return null;
        }
    }

    private static (string? Kind, int Bits) ScalarKind(ITypeSymbol type)
    {
        if (type.TypeKind == TypeKind.Enum)
        {
            return ("enum", 0);
        }
        for (var current = type as INamedTypeSymbol; current != null; current = current.BaseType)
        {
            if (IsSdkType(current, "Node") || IsSdkType(current, "Resource"))
            {
                return ("reference", 0);
            }
        }
        return type.SpecialType switch
        {
            SpecialType.System_Boolean => ("bool", 0),
            SpecialType.System_SByte => ("int", 8),
            SpecialType.System_Byte => ("uint", 8),
            SpecialType.System_Int16 => ("int", 16),
            SpecialType.System_UInt16 => ("uint", 16),
            SpecialType.System_Int32 => ("int", 32),
            SpecialType.System_UInt32 => ("uint", 32),
            SpecialType.System_Int64 => ("int", 64),
            SpecialType.System_UInt64 => ("uint", 64),
            SpecialType.System_Single => ("float", 0),
            SpecialType.System_Double => ("double", 0),
            SpecialType.System_String => ("string", 0),
            _ => type.ToDisplayString() switch
            {
                "System.Numerics.Vector2" => ("vector2", 0),
                "System.Numerics.Vector3" => ("vector3", 0),
                "System.Numerics.Vector4" => ("vector4", 0),
                "System.Numerics.Matrix4x4" => ("matrix4", 0),
                _ => (null, 0)
            }
        };
    }

    private static string Literal(string text) => Microsoft.CodeAnalysis.CSharp.SymbolDisplay.FormatLiteral(text, quote: true);

    private static string Render(INamedTypeSymbol type, List<Property> properties)
    {
        var source = new StringBuilder("// Generated by Vultra.Scripting.Generators. Do not edit.\n#nullable enable\n");
        if (!type.ContainingNamespace.IsGlobalNamespace)
        {
            source.Append("namespace ").Append(type.ContainingNamespace.ToDisplayString()).Append(";\n");
        }
        var enclosing = new Stack<INamedTypeSymbol>();
        for (var current = type.ContainingType; current != null; current = current.ContainingType)
        {
            enclosing.Push(current);
        }
        foreach (var outer in enclosing)
        {
            source.Append("partial ");
            if (outer.IsRecord)
            {
                source.Append("record ");
            }
            source.Append(outer.TypeKind == TypeKind.Struct ? "struct " : "class ").Append('@').Append(outer.Name).Append("\n{\n");
        }
        source.Append("partial class @").Append(type.Name).Append("\n{\n")
            .Append("    protected override global::Vultra.Scripting.ScriptClassDescriptor GetScriptClass()\n    {\n")
            .Append("        var properties = new global::System.Collections.Generic.List<global::Vultra.Scripting.ScriptProperty>();\n")
            .Append("        CollectScriptProperties(properties);\n")
            .Append("        return new(").Append(Literal(ClassName(type))).Append(", ").Append(Literal(NativeBase(type)!.Name))
            .Append(", properties, [");
        for (var current = type; current != null && current.ContainingAssembly.Name != "Vultra.Scripting";
             current = current.BaseType)
        {
            if (!SymbolEqualityComparer.Default.Equals(current, type))
            {
                source.Append(", ");
            }
            source.Append("typeof(").Append(current.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat)).Append(')');
        }
        source.Append("], ").Append(HasCallback(type, "_Input") ? "true" : "false")
            .Append(", ").Append(HasCallback(type, "_UnhandledInput") ? "true" : "false").Append(");\n    }\n");
        if (properties.Count != 0)
        {
            source.Append("    protected override void CollectScriptProperties(\n        global::System.Collections.Generic.List<global::Vultra.Scripting.ScriptProperty> properties)\n    {\n")
                .Append("        base.CollectScriptProperties(properties);\n");
            foreach (var property in properties)
            {
                var name = property.Symbol.Name;
                var format = SymbolDisplayFormat.FullyQualifiedFormat.WithMiscellaneousOptions(
                    SymbolDisplayMiscellaneousOptions.EscapeKeywordIdentifiers | SymbolDisplayMiscellaneousOptions.IncludeNullableReferenceTypeModifier);
                source.Append("        properties.Add(global::Vultra.Scripting.ScriptProperty.Bind<")
                    .Append(property.Type.ToDisplayString(format)).Append(">(\n            ")
                    .Append(Literal(name)).Append(", ").Append(Literal(property.Kind))
                    .Append(",\n            () => this.@").Append(name).Append(", value => this.@").Append(name).Append(" = value")
                    .Append(",\n            integerBits: ").Append(property.Bits.ToString(CultureInfo.InvariantCulture));
                if (property.ElementKind != null)
                {
                    source.Append(",\n            elementKind: ").Append(Literal(property.ElementKind));
                }
                if (property.ReferenceType != null)
                {
                    source.Append(",\n            referenceType: ").Append(Literal(property.ReferenceType))
                        .Append(", resource: ").Append(property.Resource ? "true" : "false")
                        .Append(", nullable: ").Append(property.Nullable ? "true" : "false");
                }
                if (property.ReferenceClass != null)
                {
                    source.Append(",\n            referenceClass: ").Append(Literal(property.ReferenceClass));
                    if (property.ReferenceModule != null)
                    {
                        source.Append(", referenceModule: ").Append(Literal(property.ReferenceModule));
                    }
                }
                if (property.Choices.Length != 0)
                {
                    source.Append(",\n            choices: [\n");
                    foreach (var choice in property.Choices)
                    {
                        source.Append("                new(").Append(Literal(choice.Label)).Append(", ")
                            .Append(choice.Value.ToString(CultureInfo.InvariantCulture)).Append("L),\n");
                    }
                    source.Append("            ]");
                }
                source.Append("));\n");
            }
            source.Append("    }\n");
        }
        source.Append("}\n");
        foreach (var _ in enclosing)
        {
            source.Append("}\n");
        }
        return source.ToString();
    }
}
