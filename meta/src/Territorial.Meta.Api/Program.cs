using System.Text;
using System.Text.Json;
using Microsoft.AspNetCore.Authentication.JwtBearer;
using Microsoft.AspNetCore.SignalR;
using Microsoft.IdentityModel.Tokens;
using Scalar.AspNetCore;
using Territorial.Meta.Api.Auth;
using Territorial.Meta.Api.Hubs;
using Territorial.Meta.Api.Lobbies;
using Territorial.Meta.Api.Matches;
using Territorial.Meta.Application.Lobbies;
using Territorial.Meta.Application.Matches;
using Territorial.Meta.Application.Players;
using Territorial.Meta.Infrastructure;

var builder = WebApplication.CreateBuilder(args);

#region Konfiguracja — wczytanie i walidacja

// Wszystko, co pochodzi z konfiguracji, czytane jest w jednym miejscu i PRZED rejestracją
// serwisów: brakująca albo bezsensowna wartość ma wysadzić start, a nie pierwsze żądanie.

var allowedOrigins = builder.Configuration.GetSection("Cors:AllowedOrigins").Get<string[]>() ?? [];

var connectionString =
    builder.Configuration.GetConnectionString("Meta")
    ?? throw new InvalidOperationException("Brak connection stringa 'Meta' w konfiguracji.");

var jwtOptions =
    builder.Configuration.GetSection(JwtOptions.SectionName).Get<JwtOptions>() ?? new JwtOptions();

if (jwtOptions.SigningKey.Length < JwtOptions.MinSigningKeyLength)
{
    throw new InvalidOperationException(
        $"Klucz podpisu JWT jest pusty albo krótszy niż {JwtOptions.MinSigningKeyLength} znaków. "
            + "Ustaw go poleceniem: dotnet user-secrets set \"Jwt:SigningKey\" \"<losowy ciąg>\""
    );
}

var lobbyOptions =
    builder.Configuration.GetSection(LobbyOptions.SectionName).Get<LobbyOptions>()
    ?? new LobbyOptions();

var matchOptions =
    builder.Configuration.GetSection(MatchOptions.SectionName).Get<MatchOptions>()
    ?? new MatchOptions();

#endregion

#region CORS — jawna lista origin-ów, wymuszona przez SignalR

builder.Services.AddCors(options =>
{
    options.AddPolicy(
        "CorsPolicy",
        policy =>
            policy
                .AllowAnyHeader()
                .AllowAnyMethod()
                // SignalR negocjuje połączenie żądaniem z poświadczeniami, więc bez tego
                // hub nie wstanie. Wyklucza to AllowAnyOrigin — stąd jawna lista origin-ów.
                .AllowCredentials()
                .WithOrigins(allowedOrigins)
    );
});

#endregion

#region Uwierzytelnianie — token gracza dla REST i dla huba

builder
    .Services.AddAuthentication(JwtBearerDefaults.AuthenticationScheme)
    .AddJwtBearer(options =>
    {
        // Bez tego 'sub' zostaje przemapowane na długi URI ClaimTypes.NameIdentifier
        // i GetPlayerId nie znajduje claima, który sami przed chwilą wystawiliśmy.
        options.MapInboundClaims = false;

        options.TokenValidationParameters = new TokenValidationParameters
        {
            ValidateIssuer = true,
            ValidIssuer = jwtOptions.Issuer,
            ValidateAudience = true,
            ValidAudience = jwtOptions.Audience,
            ValidateIssuerSigningKey = true,
            IssuerSigningKey = new SymmetricSecurityKey(
                Encoding.UTF8.GetBytes(jwtOptions.SigningKey)
            ),
            ValidateLifetime = true,
            ClockSkew = TimeSpan.FromSeconds(30),
        };

        options.Events = new JwtBearerEvents
        {
            // Przeglądarka nie ustawi nagłówka Authorization na handshake'u WebSocketa,
            // więc dla hubów token przyjmowany jest z query stringu — tak robi się to
            // w SignalR od zawsze. Zawężone do /hubs, żeby nie otwierać tej furtki
            // REST-owi, gdzie token lądowałby w logach dostępowych proxy.
            OnMessageReceived = context =>
            {
                string? token = context.Request.Query["access_token"];

                if (
                    !string.IsNullOrEmpty(token)
                    && context.HttpContext.Request.Path.StartsWithSegments("/hubs")
                )
                {
                    context.Token = token;
                }

                return Task.CompletedTask;
            },
        };
    });

builder.Services.AddAuthorization();

#endregion

#region Transport — REST, OpenAPI, health, SignalR

builder.Services.AddControllers();
builder.Services.AddOpenApi();
builder.Services.AddHealthChecks();

// Jawna polityka nazw, a nie poleganie na domyślnej: REST i hub muszą oddawać
// te same DTO identycznie, inaczej klient potrzebowałby dwóch zestawów typów.
builder
    .Services.AddSignalR()
    .AddJsonProtocol(options =>
        options.PayloadSerializerOptions.PropertyNamingPolicy = JsonNamingPolicy.CamelCase
    );

// Bez tego Clients.User(...) po cichu nie wysyłałoby niczego: domyślny provider czyta
// ClaimTypes.NameIdentifier, a tożsamość gracza siedzi w claimie 'sub'.
builder.Services.AddSingleton<IUserIdProvider, PlayerUserIdProvider>();

#endregion

#region Serwisy aplikacji

// Opcje wstrzykiwane jako gotowe obiekty, a nie przez IOptions: są czytane raz na start,
// nigdy się nie przeładowują, a konstruktory zostają wolne od wiedzy o konfiguracji.
builder.Services.AddSingleton(TimeProvider.System);
builder.Services.AddSingleton(jwtOptions);
builder.Services.AddSingleton(lobbyOptions);
builder.Services.AddSingleton(matchOptions);

builder.Services.AddSingleton<PlayerTokenService>();
builder.Services.AddSingleton<MatchTicketService>();

// Jedno lobby na system, więc singleton. Zegar jest jedynym miejscem, z którego wychodzi
// rozgłoszenie stanu — patrz CurrentLobby i LobbyClock.
builder.Services.AddSingleton<CurrentLobby>();
builder.Services.AddSingleton<LobbyBroadcaster>();
builder.Services.AddHostedService<LobbyClock>();

// Uruchomienie meczu jest świadomie POZA taktem zegara: zegar wpisuje zamrożony roster
// do kolejki i wraca do tykania, a launcher robi alokację, bilety i domknięcie lobby.
builder.Services.AddSingleton<MatchStartChannel>();
builder.Services.AddHostedService<MatchLauncher>();

// Porządki po poprzednim życiu procesu: mecze porzucone w trakcie alokacji.
builder.Services.AddHostedService<StaleMatchSweeper>();

builder.Services.AddScoped<GetOrCreatePlayer>();
builder.Services.AddScoped<UpdatePlayerProfile>();

// Baza, repozytoria i katalog map — szczegóły zostają w warstwie infrastruktury.
builder.Services.AddInfrastructure(connectionString);

#endregion

var app = builder.Build();

#region Pipeline żądania

// CORS przed uwierzytelnianiem: odrzucone preflight-y nie mają po co wchodzić dalej.
app.UseCors("CorsPolicy");

app.UseAuthentication();
app.UseAuthorization();

#endregion

#region Endpointy

// Dokumentacja API wyłącznie w dev — na produkcji nie ma powodu publikować schematu.
if (app.Environment.IsDevelopment())
{
    app.MapOpenApi();
    app.MapScalarApiReference();
}

app.MapControllers();
app.MapHub<LobbyHub>("/hubs/lobby");
app.MapHealthChecks("/api/health");

#endregion

app.Run();
