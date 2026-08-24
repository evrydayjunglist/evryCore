// This file is part of the TrinityCore Project. See AUTHORS for copyright information.

using System.Buffers.Binary;

namespace Evry.Playerbots.Protocol;

public static class BridgeProtocol
{
    public const int Version = 2;
    public const int MaxPayloadBytes = 64 * 1024;

    public static async ValueTask WriteFrameAsync(Stream stream, ReadOnlyMemory<byte> payload, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(stream);
        if (payload.IsEmpty || payload.Length > MaxPayloadBytes)
            throw new InvalidDataException($"Bridge payload length must be between 1 and {MaxPayloadBytes} bytes.");

        byte[] header = new byte[sizeof(uint)];
        BinaryPrimitives.WriteUInt32BigEndian(header, checked((uint)payload.Length));
        await stream.WriteAsync(header, cancellationToken).ConfigureAwait(false);
        await stream.WriteAsync(payload, cancellationToken).ConfigureAwait(false);
        await stream.FlushAsync(cancellationToken).ConfigureAwait(false);
    }

    public static async ValueTask<byte[]> ReadFrameAsync(Stream stream, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(stream);
        byte[] header = new byte[sizeof(uint)];
        await stream.ReadExactlyAsync(header, cancellationToken).ConfigureAwait(false);

        uint length = BinaryPrimitives.ReadUInt32BigEndian(header);
        if (length is 0 or > MaxPayloadBytes)
            throw new InvalidDataException($"Bridge sent invalid payload length {length}.");

        byte[] payload = new byte[length];
        await stream.ReadExactlyAsync(payload, cancellationToken).ConfigureAwait(false);
        return payload;
    }
}
