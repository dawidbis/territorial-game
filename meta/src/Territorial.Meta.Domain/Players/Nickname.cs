namespace Territorial.Meta.Domain.Players;

public readonly record struct Nickname
{
    public const int MinLength = 3;
    public const int MaxLength = 20;

    private Nickname(string value) => Value = value;

    public string Value { get; }

    public static Nickname Create(string? value) =>
        TryCreate(value, out var nickname)
            ? nickname
            : throw new ArgumentException(
                $"Nick musi mieć {MinLength}-{MaxLength} znaków i składać się z liter, cyfr, '-' lub '_'.",
                nameof(value)
            );

    public static bool TryCreate(string? value, out Nickname nickname)
    {
        nickname = default;

        if (value is null)
        {
            return false;
        }

        var trimmed = value.Trim();

        if (trimmed.Length is < MinLength or > MaxLength)
        {
            return false;
        }

        foreach (var c in trimmed)
        {
            if (!char.IsLetterOrDigit(c) && c is not ('-' or '_'))
            {
                return false;
            }
        }

        nickname = new Nickname(trimmed);
        return true;
    }

    public static Nickname FromTrusted(string value) => new(value);

    public override string ToString() => Value;
}
