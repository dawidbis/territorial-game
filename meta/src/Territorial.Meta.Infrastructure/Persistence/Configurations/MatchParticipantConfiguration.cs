using Microsoft.EntityFrameworkCore;
using Microsoft.EntityFrameworkCore.Metadata.Builders;
using Territorial.Meta.Domain.Matches;
using Territorial.Meta.Domain.Players;

namespace Territorial.Meta.Infrastructure.Persistence.Configurations;

internal sealed class MatchParticipantConfiguration : IEntityTypeConfiguration<MatchParticipant>
{
    public void Configure(EntityTypeBuilder<MatchParticipant> builder)
    {
        builder.ToTable("match_participants");

        // Klucz złożony, bez sztucznego Id: gracz jest w meczu najwyżej raz i to jest
        // dokładnie ta tożsamość, o którą pyta wydanie biletu.
        builder.HasKey(p => new { p.MatchId, p.PlayerId });

        builder.Property(p => p.MatchId).HasColumnName("match_id");

        builder.Property(p => p.PlayerId).HasColumnName("player_id");

        builder.Property(p => p.Slot).HasColumnName("slot");

        builder
            .Property(p => p.Nickname)
            .HasColumnName("nickname")
            .HasMaxLength(Nickname.MaxLength)
            // FromTrusted, nie Create — z tego samego powodu co przy graczu: zaostrzenie
            // reguł nicka nie może wysadzać ODCZYTU historycznych meczów.
            .HasConversion(nickname => nickname.Value, value => Nickname.FromTrusted(value))
            .IsRequired();

        builder.ComplexProperty(
            p => p.Color,
            color =>
            {
                color.Property(c => c.Hue).HasColumnName("color_hue");
                color.Property(c => c.Saturation).HasColumnName("color_saturation");
                color.Property(c => c.Value).HasColumnName("color_value");
            }
        );

        builder.Property(p => p.LeftAt).HasColumnName("left_at");

        // Dwóch aktorów na jednym slocie to mecz, którego nie da się odtworzyć.
        // Reguła jest w domenie, ale unikalność w bazie kosztuje jeden indeks i zamyka
        // temat na wypadek błędu w przyszłej ścieżce zapisu.
        builder
            .HasIndex(p => new { p.MatchId, p.Slot })
            .IsUnique()
            .HasDatabaseName("ix_match_participants_match_id_slot");

        // Historia meczów gracza — zapytanie, które przyjdzie razem z profilem.
        builder.HasIndex(p => p.PlayerId).HasDatabaseName("ix_match_participants_player_id");
    }
}
