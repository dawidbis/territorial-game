using Territorial.Meta.Domain.Players;

namespace Territorial.Meta.Application.Players.Contracts;

/// <summary>
/// Komenda niesie zwalidowane typy domenowe, nie surowe stringi i inty.
/// Rozbiór wejścia HTTP należy do kontrolera.
/// </summary>
public sealed record UpdatePlayerProfileCommand(Guid PlayerId, Nickname Nickname, HsvColor Color);