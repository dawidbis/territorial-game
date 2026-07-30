using Microsoft.EntityFrameworkCore;
using Microsoft.EntityFrameworkCore.Metadata.Builders;
using Territorial.Meta.Domain.Matches;

namespace Territorial.Meta.Infrastructure.Persistence.Configurations;

internal sealed class MatchConfiguration : IEntityTypeConfiguration<Match>
{
    public void Configure(EntityTypeBuilder<Match> builder)
    {
        builder.ToTable("matches");

        builder.HasKey(m => m.Id);

        builder.Property(m => m.Id).HasColumnName("id").ValueGeneratedNever();

        builder.Property(m => m.MapId).HasColumnName("map_id").HasMaxLength(64).IsRequired();

        // Enumy jako tekst, nie liczba: wiersz w bazie ma być czytelny bez zaglądania
        // do kodu, a dopisanie wartości nie może przesuwać znaczenia istniejących.
        builder
            .Property(m => m.Mode)
            .HasColumnName("mode")
            .HasConversion<string>()
            .HasMaxLength(32);

        builder
            .Property(m => m.State)
            .HasColumnName("state")
            .HasConversion<string>()
            .HasMaxLength(32);

        builder.Property(m => m.MaxActors).HasColumnName("max_actors");

        builder.Property(m => m.Seed).HasColumnName("seed");

        builder.Property(m => m.Endpoint).HasColumnName("endpoint").HasMaxLength(128);

        builder.Property(m => m.WsUrl).HasColumnName("ws_url").HasMaxLength(256);

        builder.Property(m => m.CreatedAt).HasColumnName("created_at");

        builder.Property(m => m.StartedAt).HasColumnName("started_at");

        builder.Property(m => m.EndedAt).HasColumnName("ended_at");

        builder
            .HasMany(m => m.Participants)
            .WithOne()
            .HasForeignKey(p => p.MatchId)
            // Uczestnik bez meczu nie ma znaczenia, więc kaskada, a nie osierocone wiersze.
            .OnDelete(DeleteBehavior.Cascade);

        // Kolekcja jest wystawiona jako IReadOnlyList nad prywatnym polem — EF ma pisać
        // do pola, a nie próbować dodawać do listy tylko do odczytu.
        builder.Navigation(m => m.Participants).UsePropertyAccessMode(PropertyAccessMode.Field);

        // Zamiatanie meczów, które zostały w Allocating po restarcie meta, chodzi po stanie.
        builder.HasIndex(m => m.State).HasDatabaseName("ix_matches_state");
    }
}
