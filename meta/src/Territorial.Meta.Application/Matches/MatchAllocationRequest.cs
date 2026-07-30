namespace Territorial.Meta.Application.Matches;

/// <summary>
/// Wszystko, co orkiestrator musi wiedzieć, żeby postawić proces meczu.
/// </summary>
/// <remarks>
/// Świadomie bez rostera i bez nicków: proces dostaje sloty i profile graczy dopiero
/// w <c>MatchInit</c>, a orkiestrator ma tylko wybrać maszynę i port. Im mniej wie, tym
/// mniej trzeba w nim zmieniać przy każdej zmianie modelu gracza.
/// </remarks>
public sealed record MatchAllocationRequest(Guid MatchId, string MapId, int MaxActors, long Seed);
