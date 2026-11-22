using System.Text;

// SHA-256 over 32-bit words, as in the Java and Neura versions.
internal static class Sha256
{
    private static readonly uint[] RoundConstants =
    [
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
    ];

    private static uint Rotate(uint value, int count) => (value >> count) | (value << (32 - count));

    private static void Compress(uint[] state, byte[] message, int start, uint[] schedule)
    {
        for (int t = 0; t < 16; t++)
        {
            int at = start + 4 * t;
            schedule[t] = ((uint)message[at] << 24) | ((uint)message[at + 1] << 16)
                        | ((uint)message[at + 2] << 8) | message[at + 3];
        }
        for (int t = 16; t < 64; t++)
        {
            uint w15 = schedule[t - 15];
            uint w2 = schedule[t - 2];
            uint s0 = Rotate(w15, 7) ^ Rotate(w15, 18) ^ (w15 >> 3);
            uint s1 = Rotate(w2, 17) ^ Rotate(w2, 19) ^ (w2 >> 10);
            // SHA-256 specifies that all additions of words wrap at 32 bits.
            schedule[t] = unchecked((schedule[t - 16] + s0) + (schedule[t - 7] + s1));
        }
        uint a = state[0];
        uint b = state[1];
        uint c = state[2];
        uint d = state[3];
        uint e = state[4];
        uint f = state[5];
        uint g = state[6];
        uint h = state[7];
        for (int t = 0; t < 64; t++)
        {
            uint s1 = Rotate(e, 6) ^ Rotate(e, 11) ^ Rotate(e, 25);
            uint choice = (e & f) ^ (~e & g);
            uint t1 = unchecked(((h + s1) + (choice + RoundConstants[t])) + schedule[t]);
            uint s0 = Rotate(a, 2) ^ Rotate(a, 13) ^ Rotate(a, 22);
            uint majority = (a & b) ^ (a & c) ^ (b & c);
            uint t2 = unchecked(s0 + majority);
            h = g;
            g = f;
            f = e;
            e = unchecked(d + t1);
            d = c;
            c = b;
            b = a;
            a = unchecked(t1 + t2);
        }
        unchecked
        {
            state[0] += a;
            state[1] += b;
            state[2] += c;
            state[3] += d;
            state[4] += e;
            state[5] += f;
            state[6] += g;
            state[7] += h;
        }
    }

    public static string Run(int n)
    {
        // Padding: a one bit, zeros to a 56-byte block prefix, then the bit length.
        int padded = n + 1;
        while (padded % 64 != 56)
            padded++;
        var message = new byte[padded + 8];
        for (int i = 0; i < n; i++)
            message[i] = (byte)(i % 251);
        message[n] = 128;
        long bitLength = (long)n * 8;
        for (int shift = 56, at = padded; shift >= 0; shift -= 8, at++)
            message[at] = (byte)((bitLength >>> shift) & 255);

        uint[] state =
        [
            0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
        ];
        var schedule = new uint[64];
        for (int start = 0; start < message.Length; start += 64)
            Compress(state, message, start, schedule);

        var digest = new StringBuilder();
        foreach (uint word in state)
            digest.Append(word.ToString("x8"));
        return $"sha256 {digest}\n";
    }
}
