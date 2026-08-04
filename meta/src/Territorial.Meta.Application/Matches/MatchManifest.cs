using System.Text;
using System.Text.Json;
using Territorial.Meta.Application.Players;
using Territorial.Meta.Domain.Matches;

namespace Territorial.Meta.Application.Matches;

/// <summary>
/// Roster meczu w postaci, w której czyta go proces game-serwera (plan serwera gry, §3.5).
/// </summary>
/// <remarks>
/// <para>
/// Manifest jedzie procesowi <b>stdinem</b>, a orkiestrator wyłącznie go przekazuje — nie
/// rozumie go i nie przechowuje. Stąd jego budowa stoi tutaj, po stronie meta: to jedyne
/// miejsce, które wie, co to jest nick i co to jest kolor gracza.
/// </para>
/// <para>
/// Botów w manifeście nie ma i nie będzie. Nie mają wiersza w bazie, a ich nicki i kolory
/// game-serwer wyprowadza z ziarna meczu — to warunek determinizmu (§8 dokumentu
/// architektury), nie oszczędność miejsca.
/// </para>
/// </remarks>
public static class MatchManifest
{
    /// <summary>Manifest dla uczestników meczu; pusty roster daje legalny manifest bez ludzi.</summary>
    public static string For(IReadOnlyList<MatchParticipant> participants)
    {
        ArgumentNullException.ThrowIfNull(participants);

        using var buffer = new MemoryStream();

        // Blok, żeby writer zdążył się dopisać do bufora, zanim czytamy jego zawartość.
        using (var writer = new Utf8JsonWriter(buffer))
        {
            writer.WriteStartObject();
            writer.WriteStartArray("players");

            foreach (var participant in participants)
            {
                writer.WriteStartObject();
                writer.WriteNumber("slot", participant.Slot);
                writer.WriteString("name", participant.Nickname.Value);
                writer.WriteNumber("colorRgb", participant.Color.ToRgb());
                writer.WriteEndObject();
            }

            writer.WriteEndArray();
            writer.WriteEndObject();
        }

        // Domyślne escapowanie zostaje: wszystko poza ASCII wychodzi jako \uXXXX, więc
        // manifest przechodzi przez potok niezależnie od kodowania konsoli po drodze.
        return Encoding.UTF8.GetString(buffer.ToArray());
    }
}
