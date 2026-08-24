// This file is part of the TrinityCore Project. See AUTHORS for copyright information.

using System.Text.Json;

namespace Evry.Playerbots;

internal sealed class CoordinatorWorker(ILogger<CoordinatorWorker> logger, IConfiguration configuration) : BackgroundService
{
    private static readonly TimeSpan RequestTimeout = TimeSpan.FromSeconds(10);
    private readonly string _host = configuration["Playerbot:Host"] ?? "127.0.0.1";
    private readonly int _port = ReadPort(configuration["Playerbot:Port"]);
    private readonly TimeSpan _heartbeatInterval = ReadHeartbeat(configuration["Playerbot:HeartbeatSeconds"]);

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        TimeSpan retryDelay = TimeSpan.FromSeconds(1);
        while (!stoppingToken.IsCancellationRequested)
        {
            try
            {
                await RunConnectionAsync(stoppingToken).ConfigureAwait(false);
                retryDelay = TimeSpan.FromSeconds(1);
            }
            catch (OperationCanceledException) when (stoppingToken.IsCancellationRequested)
            {
                break;
            }
            catch (Exception error)
            {
                logger.LogWarning("worldserver bridge unavailable at {Host}:{Port}: {Message}. Retrying in {DelaySeconds} second(s).",
                    _host, _port, error.Message, retryDelay.TotalSeconds);
                await Task.Delay(retryDelay, stoppingToken).ConfigureAwait(false);
                retryDelay = TimeSpan.FromSeconds(Math.Min(retryDelay.TotalSeconds * 2, 10));
            }
        }
    }

    private async Task RunConnectionAsync(CancellationToken stoppingToken)
    {
        await using BridgeClient client = await BridgeClient.ConnectAsync(_host, _port, stoppingToken).ConfigureAwait(false);
        logger.LogInformation("Connected to worldserver playerbot bridge at {Host}:{Port}.", _host, _port);

        using (JsonDocument welcome = await ExchangeWithTimeoutAsync(client, "hello", new { client = "playerbots.exe" }, stoppingToken).ConfigureAwait(false))
        {
            RequireType(welcome, "welcome");
            JsonElement payload = welcome.RootElement.GetProperty("payload");
            logger.LogInformation("Handshake complete. Protocol {ProtocolVersion}; read-only: {ReadOnly}.",
                payload.GetProperty("protocolVersion").GetInt32(), payload.GetProperty("readOnly").GetBoolean());
        }

        using (JsonDocument status = await ExchangeWithTimeoutAsync(client, "getServerStatus", null, stoppingToken).ConfigureAwait(false))
        {
            RequireType(status, "serverStatus");
            JsonElement payload = status.RootElement.GetProperty("payload");
            logger.LogInformation(
                "Realm {RealmName} ({RealmId}): playerbots enabled={Enabled}, configured={Configured}, managed={Managed}, online={Online}.",
                payload.GetProperty("realmName").GetString(), payload.GetProperty("realmId").GetUInt32(),
                payload.GetProperty("playerbotsEnabled").GetBoolean(), payload.GetProperty("configuredCount").GetInt32(),
                payload.GetProperty("managedBots").GetUInt32(), payload.GetProperty("onlineBots").GetUInt32());
        }

        using (JsonDocument roster = await ExchangeWithTimeoutAsync(client, "getBotRoster", null, stoppingToken).ConfigureAwait(false))
        {
            RequireType(roster, "botRoster");
            JsonElement bots = roster.RootElement.GetProperty("payload").GetProperty("bots");
            logger.LogInformation("worldserver reported {BotCount} managed bot(s).", bots.GetArrayLength());
            foreach (JsonElement bot in bots.EnumerateArray())
            {
                logger.LogInformation("Bot {BotId}: {Name}, level {Level}, race {Race}, class {Class}, session={SessionOnline}, in-world={InWorld}.",
                    bot.GetProperty("botId").GetUInt32(), bot.GetProperty("name").GetString(), bot.GetProperty("level").GetUInt32(),
                    bot.GetProperty("race").GetUInt32(), bot.GetProperty("class").GetUInt32(),
                    bot.GetProperty("sessionOnline").GetBoolean(), bot.GetProperty("inWorld").GetBoolean());
            }
        }

        while (!stoppingToken.IsCancellationRequested)
        {
            await Task.Delay(_heartbeatInterval, stoppingToken).ConfigureAwait(false);
            using JsonDocument pong = await ExchangeWithTimeoutAsync(client, "ping", null, stoppingToken).ConfigureAwait(false);
            RequireType(pong, "pong");
        }
    }

    private static async Task<JsonDocument> ExchangeWithTimeoutAsync(BridgeClient client, string type, object? payload,
        CancellationToken stoppingToken)
    {
        using CancellationTokenSource timeout = CancellationTokenSource.CreateLinkedTokenSource(stoppingToken);
        timeout.CancelAfter(RequestTimeout);
        return await client.ExchangeAsync(type, payload, timeout.Token).ConfigureAwait(false);
    }

    private static void RequireType(JsonDocument message, string expected)
    {
        string? actual = message.RootElement.GetProperty("type").GetString();
        if (!string.Equals(actual, expected, StringComparison.Ordinal))
            throw new InvalidDataException($"Expected {expected}, but worldserver sent {actual}.");
    }

    private static int ReadPort(string? configured)
    {
        if (configured is null)
            return 3444;
        if (int.TryParse(configured, out int port) && port is >= 1 and <= 65535)
            return port;
        throw new InvalidOperationException("Playerbot:Port must be between 1 and 65535.");
    }

    private static TimeSpan ReadHeartbeat(string? configured)
    {
        if (configured is null)
            return TimeSpan.FromSeconds(15);
        if (int.TryParse(configured, out int seconds) && seconds is >= 1 and <= 300)
            return TimeSpan.FromSeconds(seconds);
        throw new InvalidOperationException("Playerbot:HeartbeatSeconds must be between 1 and 300.");
    }
}
