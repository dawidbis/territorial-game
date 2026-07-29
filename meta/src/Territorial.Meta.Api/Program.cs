using System.Text;
using System.Text.Json;
using Microsoft.AspNetCore.Authentication.JwtBearer;
using Microsoft.IdentityModel.Tokens;
using Scalar.AspNetCore;
using Territorial.Meta.Api.Auth;
using Territorial.Meta.Api.Hubs;
using Territorial.Meta.Api.Lobbies;
using Territorial.Meta.Application.Lobbies;
using Territorial.Meta.Application.Players;
using Territorial.Meta.Infrastructure;

var builder = WebApplication.CreateBuilder(args);

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

builder.Services.AddSingleton(TimeProvider.System);
builder.Services.AddSingleton(jwtOptions);
builder.Services.AddSingleton(lobbyOptions);
builder.Services.AddSingleton<PlayerTokenService>();
builder.Services.AddSingleton<CurrentLobby>();
builder.Services.AddSingleton<LobbyBroadcaster>();
builder.Services.AddHostedService<LobbyClock>();
builder.Services.AddScoped<GetOrCreatePlayer>();
builder.Services.AddScoped<UpdatePlayerProfile>();
builder.Services.AddInfrastructure(connectionString);

var app = builder.Build();

app.UseCors("CorsPolicy");

app.UseAuthentication();
app.UseAuthorization();

if (app.Environment.IsDevelopment())
{
    app.MapOpenApi();
    app.MapScalarApiReference();
}

app.MapControllers();
app.MapHub<LobbyHub>("/hubs/lobby");
app.MapHealthChecks("/api/health");

app.Run();
