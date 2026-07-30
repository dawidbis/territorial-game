namespace Territorial.Meta.Application.Matches;

/// <summary>
/// Odpowiedź orkiestratora: gdzie stoi proces meczu.
/// </summary>
/// <remarks>
/// <para>
/// <c>Host</c> i <c>Port</c> to adres w sieci wewnętrznej — trafia na <c>Match.Endpoint</c>
/// i zostaje w meta. <c>WsUrl</c> to jedyna rzecz, którą widzi klient; zgodnie z D9 jest to
/// <c>wss://host/match/{matchId}</c>, bo cały ruch wchodzi jednym portem 443 i jest
/// routowany po ścieżce.
/// </para>
/// <para>
/// Adres publiczny buduje <b>alokator</b>, a nie warstwa wyżej: tylko on wie, czy proces
/// stoi za wspólnym proxy, czy — jak w dev — nasłuchuje wprost na swoim porcie.
/// </para>
/// </remarks>
public sealed record MatchAllocation(string Host, int Port, string WsUrl)
{
    /// <summary>Adres wewnętrzny w postaci zapisywanej na meczu.</summary>
    public string Endpoint => $"{Host}:{Port}";
}
