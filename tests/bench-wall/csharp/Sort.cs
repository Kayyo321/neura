// Same stable binary insertion sort and bottom-up merge passes as the Java
// and Neura versions. All comparisons go through the comparator delegate.
internal static class Sort
{
    private const int BlockSize = 16;
    private const long Mask = 1073741823L;

    private static void InsertionSort(int[] items, int start, int end, Comparison<int> compare)
    {
        for (int i = start + 1; i < end; i++)
        {
            int value = items[i];
            int low = start;
            int high = i;
            while (low < high)
            {
                int middle = low + (high - low) / 2;
                if (compare(items[middle], value) > 0)
                    high = middle;
                else
                    low = middle + 1;
            }
            for (int k = i; k > low; k--)
                items[k] = items[k - 1];
            items[low] = value;
        }
    }

    private static void MergePass(int[] source, int[] destination, int count, int width, Comparison<int> compare)
    {
        for (int left = 0; left < count;)
        {
            int middle = Math.Min(left + width, count);
            int end = Math.Min(middle + width, count);
            bool copyOnly = middle == end || compare(source[middle - 1], source[middle]) <= 0;
            if (copyOnly)
            {
                for (int k = left; k < end; k++)
                    destination[k] = source[k];
            }
            else
            {
                int i = left;
                int j = middle;
                int k = left;
                while (i < middle && j < end)
                {
                    int rightValue = source[j];
                    int leftValue = source[i];
                    // Taking an equal value from the left preserves stability.
                    if (compare(rightValue, leftValue) < 0)
                    {
                        destination[k] = rightValue;
                        j++;
                    }
                    else
                    {
                        destination[k] = leftValue;
                        i++;
                    }
                    k++;
                }
                while (i < middle)
                    destination[k++] = source[i++];
                while (j < end)
                    destination[k++] = source[j++];
            }
            left = end;
        }
    }

    private static void StableSort(int[] items, Comparison<int> compare)
    {
        int count = items.Length;
        for (int block = 0; block < count; block = Math.Min(block + BlockSize, count))
            InsertionSort(items, block, Math.Min(block + BlockSize, count), compare);
        if (count <= BlockSize)
            return;

        int[] buffer = (int[])items.Clone();
        bool inBuffer = false;
        for (int width = BlockSize; width < count;)
        {
            if (inBuffer)
                MergePass(buffer, items, count, width, compare);
            else
                MergePass(items, buffer, count, width, compare);
            inBuffer = !inBuffer;
            width = width > count / 2 ? count : width * 2;
        }
        if (inBuffer)
            Array.Copy(buffer, items, count);
    }

    public static string Run(int n)
    {
        var items = new int[n];
        long seed = 12345;
        for (int i = 0; i < n; i++)
        {
            seed = (seed * 1103515245L + 12345) & 2147483647L;
            items[i] = (int)(seed % 1000000);
        }
        StableSort(items, static (left, right) => left.CompareTo(right));

        long checksum = 0;
        for (int i = 0; i < items.Length; i++)
        {
            if (i > 0 && items[i - 1] > items[i])
                throw new InvalidOperationException("not sorted");
            checksum = (checksum * 31 + items[i]) & Mask;
        }
        return $"sort {checksum}\n";
    }
}
