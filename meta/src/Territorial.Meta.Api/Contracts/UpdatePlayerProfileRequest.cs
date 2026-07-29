using System.ComponentModel.DataAnnotations;

// Alias, bo właściwość Nickname przesłaniałaby typ o tej samej nazwie.
using DomainNickname = Territorial.Meta.Domain.Players.Nickname;

namespace Territorial.Meta.Api.Contracts;

public sealed class UpdatePlayerProfileRequest
{
    // Wstępne odsianie oczywistych błędów. Ostateczne słowo ma Nickname.TryCreate
    // w kontrolerze — atrybuty nie widzą przycinania białych znaków ani zestawu znaków.
    [Required]
    [StringLength(DomainNickname.MaxLength, MinimumLength = DomainNickname.MinLength)]
    public required string Nickname { get; init; }

    [Required]
    public required HsvColorRequest Color { get; init; }
}
