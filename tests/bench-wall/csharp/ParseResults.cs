// Same workload as the Java and Neura versions, with nullable value types
// for Optional and a tagged hierarchy for the parse Result.
internal static class ParseResults
{
    private const int Records = 1000;
    private const long Mask = 1073741823L;
    private static readonly string[] Keys = ["id", "size", "count", "weight"];

    private enum ParseErrorKind { Empty, InvalidCharacter, OutOfRange }
    private sealed record ParseError(ParseErrorKind Kind, int Offset, string Message);
    private abstract record IntResult;
    private sealed record Ok(int Value) : IntResult;
    private sealed record Err(ParseError Error) : IntResult;

    private static int? Separator(string line)
    {
        for (int i = 0; i < line.Length; i++)
        {
            if (line[i] == '=')
                return i == 0 ? null : i;
        }
        return null;
    }

    private static IntResult Failure(ParseErrorKind kind, int offset, string message) =>
        new Err(new ParseError(kind, offset, message));

    // Read an optional minus and between one and nine decimal digits.
    private static IntResult ParseInteger(string line, int start, int end)
    {
        int at = start;
        bool negative = at < end && line[at] == '-';
        if (negative)
            at++;
        if (at == end)
            return Failure(ParseErrorKind.Empty, at, "empty number");
        if (end - at > 9)
            return Failure(ParseErrorKind.OutOfRange, at, "too many digits");

        int value = 0;
        for (; at < end; at++)
        {
            char digit = line[at];
            if (digit < '0' || digit > '9')
                return Failure(ParseErrorKind.InvalidCharacter, at, "invalid digit");
            value = value * 10 + (digit - '0');
        }
        return new Ok(negative ? -value : value);
    }

    private static long? KeyWeight(string key) => key switch
    {
        "id" => 1L,
        "size" => 3L,
        "count" => 5L,
        "weight" => 7L,
        _ => null,
    };

    public static string Run(int n)
    {
        var lines = new List<string>();
        long seed = 12345;
        for (int i = 0; i < Records; i++)
        {
            seed = (seed * 1103515245L + 12345) & 2147483647L;
            string key = Keys[(int)(seed % 4)];
            long number = (seed >> 4) % 1000000;
            long kind = (seed >> 24) % 10;
            lines.Add(kind switch
            {
                0 => $"{key}{number}",
                1 => $"{key}={number}x",
                2 => $"{key}=",
                3 => $"colour={number}",
                4 => $"{key}={number}0000000000",
                5 => $"{key}=-{number}",
                _ => $"{key}={number}",
            });
        }

        int parsed = 0;
        int missing = 0;
        int invalid = 0;
        int unknown = 0;
        long checksum = 0;
        for (int round = 0; round < n; round++)
        {
            for (int i = 0; i < lines.Count; i++)
            {
                string line = lines[i];
                if (Separator(line) is not int at)
                {
                    missing++;
                    continue;
                }
                switch (ParseInteger(line, at + 1, line.Length))
                {
                    case Ok ok:
                        if (KeyWeight(line[..at]) is long weight)
                        {
                            parsed++;
                            checksum = (checksum * 31 + ok.Value * weight + round) & Mask;
                        }
                        else
                        {
                            unknown++;
                        }
                        break;
                    case Err err:
                        invalid++;
                        checksum = (checksum * 31 + err.Error.Offset) & Mask;
                        break;
                }
            }
        }
        return $"parse-results {parsed} {missing} {invalid} {unknown} {checksum}\n";
    }
}
