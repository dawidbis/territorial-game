using Territorial.Meta.Application.Players.Contracts;

namespace Territorial.Meta.Application.Lobbies.Contracts;

public sealed record LobbyPlayerDto(Guid PlayerId, string Nickname, HsvColorDto Color);
