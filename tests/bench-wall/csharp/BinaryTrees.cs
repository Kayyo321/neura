using System.Text;

internal static class BinaryTrees
{
    private const int MinDepth = 4;

    private sealed class TreeNode
    {
        public TreeNode? Left;
        public TreeNode? Right;
    }

    private static TreeNode BottomUp(int depth)
    {
        var node = new TreeNode();
        if (depth > 0)
        {
            node.Left = BottomUp(depth - 1);
            node.Right = BottomUp(depth - 1);
        }
        return node;
    }

    private static int Check(TreeNode node)
    {
        return node.Left is null ? 1 : 1 + Check(node.Left) + Check(node.Right!);
    }

    public static string Run(int n)
    {
        var output = new StringBuilder();
        int maxDepth = Math.Max(n, MinDepth + 2);
        int stretchDepth = maxDepth + 1;
        output.Append($"stretch tree of depth {stretchDepth} check: {Check(BottomUp(stretchDepth))}\n");
        TreeNode longLived = BottomUp(maxDepth);
        for (int depth = MinDepth; depth <= maxDepth; depth += 2)
        {
            int iterations = 1 << (maxDepth - depth + MinDepth);
            int total = 0;
            for (int i = 0; i < iterations; i++)
                total += Check(BottomUp(depth));

            output.Append($"{iterations} trees of depth {depth} check: {total}\n");
        }
        output.Append($"long lived tree of depth {maxDepth} check: {Check(longLived)}\n");
        return output.ToString();
    }
}
