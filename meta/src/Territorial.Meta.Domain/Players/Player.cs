namespace Territorial.Meta.Domain.Players;

/// <summary>
/// Gracz — tożsamość utrwalana od pierwszej wizyty na stronie. Nazwa "Player", a nie "User",
/// jest celowa: konto logowania dojdzie później jako osobna encja dowiązana do tego gracza,
/// dzięki czemu gość zakładający konto zachowa nick, kolor i historię.
/// </summary>
public sealed class Player
{
    /// <summary>Prefiks nicka nadawanego przy pierwszej wizycie.</summary>
    public const string GuestNicknamePrefix = "Guest";

    /// <summary>
    /// Ziarnistość <see cref="LastSeenAt"/>. Poniżej tego progu <see cref="Touch"/> nic nie robi —
    /// inaczej każde GET /players/me byłoby zapisem do bazy.
    /// </summary>
    public static readonly TimeSpan LastSeenPrecision = TimeSpan.FromMinutes(5);

    /// <summary>Ile końcowych znaków identyfikatora trafia do nicka gościa.</summary>
    private const int GuestSuffixLength = 6;

    /// <summary>Ile końcowych znaków sufiksu wyznacza odcień koloru.</summary>
    private const int HueSourceLength = 4;

    private Player()
    {
        // Konstruktor dla EF Core. Wszystkie właściwości są typami wartościowymi,
        // więc nie ma tu żadnego "= null!".
    }

    private Player(Guid id, Nickname nickname, HsvColor color, DateTimeOffset now)
    {
        Id = id;
        Nickname = nickname;
        Color = color;
        CreatedAt = now;
        LastSeenAt = now;
    }

    public Guid Id { get; private set; }

    public Nickname Nickname { get; private set; }

    public HsvColor Color { get; private set; }

    public DateTimeOffset CreatedAt { get; private set; }

    public DateTimeOffset LastSeenAt { get; private set; }

    /// <summary>
    /// Tworzy gracza-gościa. Nick i kolor są wyprowadzone z losowej części identyfikatora,
    /// więc od pierwszej chwili mają konkretne wartości. W systemie nie istnieje pojęcie
    /// "wartości domyślnej" ani flaga "czy gracz to zmieniał".
    /// </summary>
    public static Player CreateGuest(DateTimeOffset now)
    {
        var id = Guid.CreateVersion7(now);

        // Znaki KOŃCOWE, nie początkowe. Pierwsze 12 znaków UUIDv7 to 48-bitowy znacznik
        // czasu — identyczny dla wszystkich graczy przez kilkadziesiąt dni. Losowość
        // (rand_b, 62 bity z CSPRNG) siedzi w końcówce.
        var suffix = id.ToString("N")[^GuestSuffixLength..].ToUpperInvariant();

        var nickname = Nickname.Create(GuestNicknamePrefix + suffix);
        var hue = Convert.ToInt32(suffix[^HueSourceLength..], 16) % HsvColor.HueCount;
        var color = HsvColor.Create(hue, HsvColor.DefaultSaturation, HsvColor.DefaultValue);

        return new Player(id, nickname, color, now);
    }

    public void Rename(Nickname nickname) => Nickname = nickname;

    public void ChangeColor(HsvColor color) => Color = color;

    /// <summary>Odnotowuje wizytę gracza.</summary>
    /// <returns>
    /// <c>true</c>, jeśli znacznik faktycznie się przesunął. Warstwa aplikacji zapisuje
    /// zmiany tylko wtedy — dzięki temu odczyt profilu zwykle nie generuje UPDATE.
    /// </returns>
    public bool Touch(DateTimeOffset now)
    {
        if (now - LastSeenAt < LastSeenPrecision)
        {
            return false;
        }

        LastSeenAt = now;
        return true;
    }
}