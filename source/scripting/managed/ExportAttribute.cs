using System;

namespace Vultra.Scripting;

[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class ExportAttribute : Attribute
{
    public bool Nullable { get; set; } = true;
    public string? ReferenceClass { get; set; }
    public string? ReferenceModule { get; set; }
}
