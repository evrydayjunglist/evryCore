// This file is part of the TrinityCore Project. See AUTHORS for copyright information.

using System.Net.Sockets;
using System.Text.Json;
using Evry.Playerbots.Protocol;

namespace Evry.Playerbots;

internal sealed class BridgeClient : IAsyncDisposable
{
    private readonly TcpClient _client;
    private readonly NetworkStream _stream;
    private long _nextRequestId;

    private BridgeClient(TcpClient client)
    {
        _client = client;
        _client.NoDelay = true;
        _stream = client.GetStream();
    }

    public static async Task<BridgeClient> ConnectAsync(string host, int port, CancellationToken cancellationToken)
    {
        TcpClient client = new();
        try
        {
            await client.ConnectAsync(host, port, cancellationToken).ConfigureAwait(false);
            return new BridgeClient(client);
        }
        catch
        {
            client.Dispose();
            throw;
        }
    }

    public async Task<JsonDocument> ExchangeAsync(string type, object? payload, CancellationToken cancellationToken)
    {
        string requestId = Interlocked.Increment(ref _nextRequestId).ToString(System.Globalization.CultureInfo.InvariantCulture);
        byte[] request = JsonSerializer.SerializeToUtf8Bytes(new
        {
            version = BridgeProtocol.Version,
            type,
            requestId,
            payload = payload ?? new { }
        });

        await BridgeProtocol.WriteFrameAsync(_stream, request, cancellationToken).ConfigureAwait(false);
        byte[] responseBytes = await BridgeProtocol.ReadFrameAsync(_stream, cancellationToken).ConfigureAwait(false);
        JsonDocument response = JsonDocument.Parse(responseBytes);

        try
        {
            JsonElement root = response.RootElement;
            int version = root.GetProperty("version").GetInt32();
            string? responseRequestId = root.GetProperty("requestId").GetString();
            if (version != BridgeProtocol.Version)
                throw new InvalidDataException($"worldserver replied with protocol version {version}.");
            if (!string.Equals(requestId, responseRequestId, StringComparison.Ordinal))
                throw new InvalidDataException($"worldserver replied to request {responseRequestId} while waiting for {requestId}.");

            if (root.GetProperty("type").GetString() == "error")
            {
                JsonElement error = root.GetProperty("payload");
                throw new InvalidDataException($"worldserver rejected {type}: {error.GetProperty("code").GetString()} - {error.GetProperty("message").GetString()}");
            }

            return response;
        }
        catch
        {
            response.Dispose();
            throw;
        }
    }

    public ValueTask DisposeAsync()
    {
        _stream.Dispose();
        _client.Dispose();
        return ValueTask.CompletedTask;
    }
}
