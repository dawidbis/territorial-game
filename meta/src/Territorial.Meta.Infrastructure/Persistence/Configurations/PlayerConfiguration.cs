using Microsoft.EntityFrameworkCore;
using Microsoft.EntityFrameworkCore.Metadata.Builders;
using Territorial.Meta.Domain.Players;

namespace Territorial.Meta.Infrastructure.Persistence.Configurations;

internal sealed class PlayerConfiguration : IEntityTypeConfiguration<Player>
{
    public void Configure(EntityTypeBuilder<Player> builder)
    {
        builder.ToTable("players");

        builder.HasKey(p => p.Id);

        builder.Property(p => p.Id).HasColumnName("id").ValueGeneratedNever();

        builder
            .Property(p => p.Nickname)
            .HasColumnName("nickname")
            .HasMaxLength(Nickname.MaxLength)
            .HasConversion(
                nickname => nickname.Value,
                // FromTrusted, nie Create: walidacja należy do wejścia, nie do
                // materializacji. Gdyby reguły nicka kiedyś się zaostrzyły, Create
                // wysadzałby ODCZYT istniejących wierszy.
                value => Nickname.FromTrusted(value)
            )
            .IsRequired();

        // Typ złożony, nie owned entity — typy owned muszą być referencyjne,
        // a HsvColor jest readonly record struct. Mapuje się na trzy kolumny
        // tej samej tabeli, bez sztucznej tożsamości.
        builder.ComplexProperty(
            p => p.Color,
            color =>
            {
                color.Property(c => c.Hue).HasColumnName("color_hue");
                color.Property(c => c.Saturation).HasColumnName("color_saturation");
                color.Property(c => c.Value).HasColumnName("color_value");
            }
        );

        builder.Property(p => p.CreatedAt).HasColumnName("created_at");

        builder.Property(p => p.LastSeenAt).HasColumnName("last_seen_at");
    }
}
