// This file is part of the TrinityCore Project. See AUTHORS for copyright information.

using System.Buffers.Binary;
using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using Evry.Playerbots.Protocol;

await RoundTripsFrameAsync();
await RejectsEmptyPayloadAsync();
await RejectsOversizedPayloadAsync();
await RejectsInvalidIncomingLengthAsync();
await RejectsTruncatedPayloadAsync();
Console.WriteLine("Playerbots.Protocol framing tests passed.");

if (args is ["--host", string hostExecutable])
{
    await ReconnectsHostAsync(hostExecutable);
    Console.WriteLine("playerbots.exe reconnect integration test passed.");
}

static async Task RoundTripsFrameAsync()
{
    byte[] payload = Encoding.UTF8.GetBytes("{\"version\":1,\"type\":\"ping\"}");
    await using MemoryStream stream = new();
    await BridgeProtocol.WriteFrameAsync(stream, payload, CancellationToken.None);

    byte[] frame = stream.ToArray();
    Assert(BinaryPrimitives.ReadUInt32BigEndian(frame.AsSpan(0, 4)) == payload.Length, "Frame length must use big-endian order.");
    stream.Position = 0;
    byte[] decoded = await BridgeProtocol.ReadFrameAsync(stream, CancellationToken.None);
    Assert(payload.AsSpan().SequenceEqual(decoded), "Frame payload did not round-trip.");
}

static async Task RejectsEmptyPayloadAsync()
{
    await AssertThrowsAsync<InvalidDataException>(async () =>
        await BridgeProtocol.WriteFrameAsync(Stream.Null, ReadOnlyMemory<byte>.Empty, CancellationToken.None));
}

static async Task RejectsOversizedPayloadAsync()
{
    byte[] oversized = new byte[BridgeProtocol.MaxPayloadBytes + 1];
    await AssertThrowsAsync<InvalidDataException>(async () =>
        await BridgeProtocol.WriteFrameAsync(Stream.Null, oversized, CancellationToken.None));
}

static async Task RejectsInvalidIncomingLengthAsync()
{
    await using MemoryStream stream = new(new byte[4]);
    await AssertThrowsAsync<InvalidDataException>(async () =>
        await BridgeProtocol.ReadFrameAsync(stream, CancellationToken.None));
}

static async Task RejectsTruncatedPayloadAsync()
{
    byte[] frame = new byte[6];
    BinaryPrimitives.WriteUInt32BigEndian(frame.AsSpan(0, 4), 3);
    await using MemoryStream stream = new(frame);
    await AssertThrowsAsync<EndOfStreamException>(async () =>
        await BridgeProtocol.ReadFrameAsync(stream, CancellationToken.None));
}

static async Task ReconnectsHostAsync(string hostExecutable)
{
    string executablePath = Path.GetFullPath(hostExecutable);
    if (!File.Exists(executablePath))
        throw new FileNotFoundException("playerbots.exe was not found.", executablePath);

    using TcpListener listener = new(IPAddress.Loopback, 0);
    listener.Start();
    int port = ((IPEndPoint)listener.LocalEndpoint).Port;

    ProcessStartInfo startInfo = new(executablePath)
    {
        WorkingDirectory = Path.GetDirectoryName(executablePath)!,
        RedirectStandardOutput = true,
        RedirectStandardError = true,
        UseShellExecute = false
    };
    startInfo.ArgumentList.Add($"--Playerbot:Port={port}");
    startInfo.ArgumentList.Add("--Playerbot:HeartbeatSeconds=1");

    using Process process = Process.Start(startInfo) ?? throw new InvalidOperationException("Could not start playerbots.exe.");
    Task<string> standardOutput = process.StandardOutput.ReadToEndAsync();
    Task<string> standardError = process.StandardError.ReadToEndAsync();
    using CancellationTokenSource timeout = new(TimeSpan.FromSeconds(10));

    try
    {
        await HandleHostConnectionAsync(listener, timeout.Token);
        await HandleHostConnectionAsync(listener, timeout.Token);
    }
    catch (Exception error)
    {
        if (!process.HasExited)
            process.Kill(entireProcessTree: true);
        await process.WaitForExitAsync();
        throw new InvalidOperationException(
            $"playerbots.exe integration failed: {error.Message}{Environment.NewLine}{await standardOutput}{await standardError}", error);
    }
    finally
    {
        if (!process.HasExited)
            process.Kill(entireProcessTree: true);
        await process.WaitForExitAsync();
    }
}

static async Task HandleHostConnectionAsync(TcpListener listener, CancellationToken cancellationToken)
{
    using TcpClient client = await listener.AcceptTcpClientAsync(cancellationToken);
    await using NetworkStream stream = client.GetStream();

    using (JsonDocument hello = await ReadMessageAsync(stream, cancellationToken))
    {
        AssertMessageType(hello, "hello");
        await WriteResponseAsync(stream, hello, "welcome", new
        {
            protocolVersion = BridgeProtocol.Version,
            readOnly = false,
            loginMode = "Coordinator",
            server = "worldserver"
        }, cancellationToken);
    }

    using (JsonDocument status = await ReadMessageAsync(stream, cancellationToken))
    {
        AssertMessageType(status, "getServerStatus");
        await WriteResponseAsync(stream, status, "serverStatus", new
        {
            playerbotsEnabled = true,
            loginMode = "Coordinator",
            configuredCount = 1,
            managedBots = 1,
            onlineBots = 0,
            activeSessions = 0,
            onlinePlayers = 0,
            realmId = 1,
            realmName = "Test Realm"
        }, cancellationToken);
    }

    using (JsonDocument roster = await ReadMessageAsync(stream, cancellationToken))
    {
        AssertMessageType(roster, "getBotRoster");
        await WriteResponseAsync(stream, roster, "botRoster", new
        {
            bots = new[]
            {
                new
                {
                    botId = 1,
                    accountId = 100,
                    characterGuid = "Player-1-00000001",
                    name = "Opai",
                    race = 2,
                    @class = 1,
                    level = 10,
                    sessionOnline = false,
                    inWorld = false
                }
            }
        }, cancellationToken);
    }

    using (JsonDocument ensure = await ReadMessageAsync(stream, cancellationToken))
    {
        AssertMessageType(ensure, "ensureBotsOnline");
        JsonElement payload = ensure.RootElement.GetProperty("payload");
        Assert(payload.ValueKind == JsonValueKind.Object && !payload.EnumerateObject().Any(),
            "ensureBotsOnline must not send writable roster parameters.");
        await WriteResponseAsync(stream, ensure, "botsOnlineEnsured", new
        {
            managedBots = 1,
            onlineBots = 0,
            loginRequestsStarted = 1
        }, cancellationToken);
    }
}

static async Task<JsonDocument> ReadMessageAsync(Stream stream, CancellationToken cancellationToken)
{
    byte[] payload = await BridgeProtocol.ReadFrameAsync(stream, cancellationToken);
    return JsonDocument.Parse(payload);
}

static async Task WriteResponseAsync(Stream stream, JsonDocument request, string type, object payload,
    CancellationToken cancellationToken)
{
    byte[] response = JsonSerializer.SerializeToUtf8Bytes(new
    {
        version = BridgeProtocol.Version,
        type,
        requestId = request.RootElement.GetProperty("requestId").GetString(),
        payload
    });
    await BridgeProtocol.WriteFrameAsync(stream, response, cancellationToken);
}

static void AssertMessageType(JsonDocument message, string expected)
{
    string? actual = message.RootElement.GetProperty("type").GetString();
    Assert(actual == expected, $"Expected {expected}, but received {actual}.");
}

static async Task AssertThrowsAsync<T>(Func<Task> action) where T : Exception
{
    try
    {
        await action();
    }
    catch (T)
    {
        return;
    }

    throw new InvalidOperationException($"Expected {typeof(T).Name}.");
}

static void Assert(bool condition, string message)
{
    if (!condition)
        throw new InvalidOperationException(message);
}
