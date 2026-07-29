namespace Territorial.Meta.Domain.Lobbies;

/// <summary>
/// Mapa dostępna w katalogu — na tym etapie wyłącznie to, czego potrzebuje lobby.
/// Wymiary, sha256, ścieżka CDN i punkty startowe (dokument §4.2) dojdą razem
/// z game-serwerem, bo dopiero on ma z nich jakikolwiek pożytek.
/// </summary>
/// <param name="Id">Stabilny identyfikator, docelowo część ścieżki assetu.</param>
/// <param name="Name">Nazwa pokazywana graczom.</param>
/// <param name="MaxActors">
/// Sufit aktorów na mapie — ludzie i boty razem. Patrz D12: przedział 1..254.
/// Jeśli kiedyś ludzie mają mieć własny, niższy limit (dokument mówi o 10–64),
/// dochodzi tu drugie pole; reszta kodu liczy już wtedy z właściwego.
/// </param>
public sealed record MapDefinition(string Id, string Name, int MaxActors);
