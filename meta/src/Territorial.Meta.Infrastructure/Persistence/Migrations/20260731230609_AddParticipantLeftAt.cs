using System;
using Microsoft.EntityFrameworkCore.Migrations;

#nullable disable

namespace Territorial.Meta.Infrastructure.Persistence.Migrations
{
    /// <inheritdoc />
    public partial class AddParticipantLeftAt : Migration
    {
        /// <inheritdoc />
        protected override void Up(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.AddColumn<DateTimeOffset>(
                name: "left_at",
                table: "match_participants",
                type: "TEXT",
                nullable: true);
        }

        /// <inheritdoc />
        protected override void Down(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.DropColumn(
                name: "left_at",
                table: "match_participants");
        }
    }
}
