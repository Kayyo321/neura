internal static class Fannkuch
{
    public static string Run(int n)
    {
        int[] perm = new int[n];
        int[] perm1 = new int[n];
        int[] count = new int[n];
        for (int i = 0; i < n; i++)
            perm1[i] = i;

        int maxFlips = 0;
        int checksum = 0;
        int permutation = 0;
        int r = n;
        while (true)
        {
            while (r != 1)
            {
                count[r - 1] = r;
                r--;
            }
            for (int i = 0; i < n; i++)
                perm[i] = perm1[i];

            int flips = 0;
            int k = perm[0];
            while (k != 0)
            {
                int half = (k + 1) / 2;
                for (int i = 0; i < half; i++)
                {
                    int swapped = perm[i];
                    perm[i] = perm[k - i];
                    perm[k - i] = swapped;
                }
                flips++;
                k = perm[0];
            }
            if (flips > maxFlips)
                maxFlips = flips;
            if (permutation % 2 == 0)
                checksum += flips;
            else
                checksum -= flips;

            // Rotate prefixes of perm1 to reach the next permutation.
            while (true)
            {
                if (r == n)
                    return $"fannkuch {checksum} {maxFlips}\n";

                int first = perm1[0];
                for (int i = 0; i < r; i++)
                    perm1[i] = perm1[i + 1];

                perm1[r] = first;
                count[r]--;
                if (count[r] > 0)
                    break;

                r++;
            }
            permutation++;
        }
    }
}
