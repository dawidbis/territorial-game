namespace Territorial.Meta.Application.Players.Contracts;

public sealed record PlayerProfileDto(Guid Id, string Nickname, HsvColorDto Color);