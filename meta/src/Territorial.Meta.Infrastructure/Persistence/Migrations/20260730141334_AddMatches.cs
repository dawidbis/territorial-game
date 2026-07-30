using System;
using Microsoft.EntityFrameworkCore.Migrations;

#nullable disable

namespace Territorial.Meta.Infrastructure.Persistence.Migrations
{
    /// <inheritdoc />
    public partial class AddMatches : Migration
    {
        /// <inheritdoc />
        protected override void Up(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.CreateTable(
                name: "matches",
                columns: table => new
                {
                    id = table.Column<Guid>(type: "TEXT", nullable: false),
                    map_id = table.Column<string>(type: "TEXT", maxLength: 64, nullable: false),
                    mode = table.Column<string>(type: "TEXT", maxLength: 32, nullable: false),
                    max_actors = table.Column<int>(type: "INTEGER", nullable: false),
                    seed = table.Column<long>(type: "INTEGER", nullable: false),
                    endpoint = table.Column<string>(type: "TEXT", maxLength: 128, nullable: true),
                    ws_url = table.Column<string>(type: "TEXT", maxLength: 256, nullable: true),
                    state = table.Column<string>(type: "TEXT", maxLength: 32, nullable: false),
                    created_at = table.Column<DateTimeOffset>(type: "TEXT", nullable: false),
                    started_at = table.Column<DateTimeOffset>(type: "TEXT", nullable: true),
                    ended_at = table.Column<DateTimeOffset>(type: "TEXT", nullable: true)
                },
                constraints: table =>
                {
                    table.PrimaryKey("PK_matches", x => x.id);
                });

            migrationBuilder.CreateTable(
                name: "match_participants",
                columns: table => new
                {
                    match_id = table.Column<Guid>(type: "TEXT", nullable: false),
                    player_id = table.Column<Guid>(type: "TEXT", nullable: false),
                    slot = table.Column<byte>(type: "INTEGER", nullable: false),
                    nickname = table.Column<string>(type: "TEXT", maxLength: 20, nullable: false),
                    color_hue = table.Column<int>(type: "INTEGER", nullable: false),
                    color_saturation = table.Column<int>(type: "INTEGER", nullable: false),
                    color_value = table.Column<int>(type: "INTEGER", nullable: false)
                },
                constraints: table =>
                {
                    table.PrimaryKey("PK_match_participants", x => new { x.match_id, x.player_id });
                    table.ForeignKey(
                        name: "FK_match_participants_matches_match_id",
                        column: x => x.match_id,
                        principalTable: "matches",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Cascade);
                });

            migrationBuilder.CreateIndex(
                name: "ix_match_participants_match_id_slot",
                table: "match_participants",
                columns: new[] { "match_id", "slot" },
                unique: true);

            migrationBuilder.CreateIndex(
                name: "ix_match_participants_player_id",
                table: "match_participants",
                column: "player_id");

            migrationBuilder.CreateIndex(
                name: "ix_matches_state",
                table: "matches",
                column: "state");
        }

        /// <inheritdoc />
        protected override void Down(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.DropTable(
                name: "match_participants");

            migrationBuilder.DropTable(
                name: "matches");
        }
    }
}
