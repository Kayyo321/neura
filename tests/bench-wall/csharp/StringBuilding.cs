using System.Text;

// Materialize the same UTF-8 bytes as the Java and Neura versions.
internal static class StringBuilding
{
    private const long Mask = 1073741823L;

    public static string Run(int n)
    {
        var builder = new StringBuilder();
        for (int i = 0; i < n; i++)
            builder.Append("item ").Append((long)i).Append(';');
        byte[] text = Encoding.UTF8.GetBytes(builder.ToString());
        long checksum = 0;
        foreach (byte value in text)
            checksum = (checksum * 31 + value) & Mask;
        return $"string-building {text.Length} {checksum}\n";
    }
}
