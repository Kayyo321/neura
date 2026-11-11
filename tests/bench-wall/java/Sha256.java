// Same algorithm, input, and output as ../neura/sha256.nra: SHA-256 written
// out over 32-bit words, not java.security.MessageDigest.
public final class Sha256 {
    private static final int[] ROUND_CONSTANTS = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
    };

    private static int rotate(int value, int count) {
        return (value >>> count) | (value << (32 - count));
    }

    // Processes the 64-byte block of `message` that starts at `start`.
    private static void compress(int[] state, byte[] message, int start, int[] schedule) {
        for (int t = 0; t < 16; t = t + 1) {
            int at = start + 4 * t;
            schedule[t] = ((message[at] & 0xff) << 24) | ((message[at + 1] & 0xff) << 16)
                          | ((message[at + 2] & 0xff) << 8) | (message[at + 3] & 0xff);
        }
        for (int t = 16; t < 64; t = t + 1) {
            int w15 = schedule[t - 15];
            int w2 = schedule[t - 2];
            int s0 = rotate(w15, 7) ^ rotate(w15, 18) ^ (w15 >>> 3);
            int s1 = rotate(w2, 17) ^ rotate(w2, 19) ^ (w2 >>> 10);
            schedule[t] = (schedule[t - 16] + s0) + (schedule[t - 7] + s1);
        }
        int a = state[0];
        int b = state[1];
        int c = state[2];
        int d = state[3];
        int e = state[4];
        int f = state[5];
        int g = state[6];
        int h = state[7];
        for (int t = 0; t < 64; t = t + 1) {
            int s1 = rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25);
            int choice = (e & f) ^ (~e & g);
            int t1 = ((h + s1) + (choice + ROUND_CONSTANTS[t])) + schedule[t];
            int s0 = rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22);
            int majority = (a & b) ^ (a & c) ^ (b & c);
            int t2 = s0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        state[0] = state[0] + a;
        state[1] = state[1] + b;
        state[2] = state[2] + c;
        state[3] = state[3] + d;
        state[4] = state[4] + e;
        state[5] = state[5] + f;
        state[6] = state[6] + g;
        state[7] = state[7] + h;
    }

    public static void main(String[] args) {
        int n = Integer.parseInt(args[0]);
        // Padding: a one bit, zeros to 56 mod 64 bytes, then the bit length.
        int padded = n + 1;
        while (padded % 64 != 56) {
            padded = padded + 1;
        }
        byte[] message = new byte[padded + 8];
        for (int i = 0; i < n; i = i + 1) {
            message[i] = (byte) (i % 251);
        }
        message[n] = (byte) 128;
        long bitLength = (long) n * 8;
        for (int shift = 56, at = padded; shift >= 0; shift = shift - 8, at = at + 1) {
            message[at] = (byte) ((bitLength >>> shift) & 255);
        }
        int[] state = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab,
                       0x5be0cd19};
        int[] schedule = new int[64];
        for (int start = 0; start < message.length; start = start + 64) {
            compress(state, message, start, schedule);
        }
        StringBuilder digest = new StringBuilder();
        for (int i = 0; i < 8; i = i + 1) {
            digest.append(String.format("%08x", state[i]));
        }
        System.out.println("sha256 " + digest);
    }
}
