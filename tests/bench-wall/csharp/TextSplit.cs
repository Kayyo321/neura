using System.Text;

// Materialize the same ASCII pieces as the Java and Neura versions.
internal static class TextSplit
{
    private const int Fields = 8;
    private const long Mask = 1073741823L;
    private static readonly string[] Words = ["alpha", "beta", "gamma", "delta"];

    public static string VerifyEdges()
    {
        (string Value, string Separator)[] inputs = [("", ","), ("single", ","), (",,", ","), ("é🔹猫🔹", "🔹")];
        StringBuilder output = new();
        foreach (var (value, separator) in inputs)
        {
            string[] parts = value.Split(separator, StringSplitOptions.None);
            string joined = string.Join("|", parts);
            output.Append(parts.Length).Append(' ').Append(Encoding.UTF8.GetByteCount(joined))
                .Append(' ').Append(joined).Append('\n');
        }
        return output.ToString();
    }

    public static string Run(int n)
    {
        long seed = 12345;
        int matches = 0;
        long bytes = 0;
        long checksum = 0;
        for (int i = 0; i < n; i++)
        {
            var line = new StringBuilder();
            for (int field = 0; field < Fields; field++)
            {
                seed = (seed * 1103515245L + 12345) & 2147483647L;
                if (field > 0)
                    line.Append(',');
                for (long pad = 0; pad < seed % 3; pad++)
                    line.Append(' ');
                if (seed % 4 == 0)
                    line.Append(Words[(int)((seed >> 8) % 4)]);
                else
                    line.Append((seed >> 8) % 200000 - 100000);
                if ((seed >> 4) % 2 == 0)
                    line.Append("  ");
            }

            string[] parts = line.ToString().Split(',', StringSplitOptions.None);
            var fields = new List<string>();
            foreach (string part in parts)
            {
                string trimmed = part.Trim();
                if (trimmed == "beta")
                    matches++;
                fields.Add(trimmed);
            }
            string joined = string.Join('|', fields);
            bytes += joined.Length;
            foreach (char value in joined)
                checksum = (checksum * 31 + value) & Mask;
        }
        return $"text-split {matches} {bytes} {checksum}\n";
    }
}
