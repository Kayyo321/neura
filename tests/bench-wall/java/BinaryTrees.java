// Same algorithm, input, and output as ../neura/binary-trees.nra.
public final class BinaryTrees {
    private static final int MIN_DEPTH = 4;

    private static final class TreeNode {
        TreeNode left;
        TreeNode right;
    }

    private static TreeNode bottomUp(int depth) {
        TreeNode node = new TreeNode();
        if (depth > 0) {
            node.left = bottomUp(depth - 1);
            node.right = bottomUp(depth - 1);
        }
        return node;
    }

    private static int check(TreeNode node) {
        if (node.left == null) {
            return 1;
        }
        return 1 + check(node.left) + check(node.right);
    }

    public static void main(String[] args) {
        int n = Integer.parseInt(args[0]);
        int maxDepth = n;
        if (maxDepth < MIN_DEPTH + 2) {
            maxDepth = MIN_DEPTH + 2;
        }
        int stretchDepth = maxDepth + 1;
        System.out.println("stretch tree of depth " + stretchDepth + " check: " + check(bottomUp(stretchDepth)));
        TreeNode longLived = bottomUp(maxDepth);
        for (int depth = MIN_DEPTH; depth <= maxDepth; depth = depth + 2) {
            int iterations = 1 << (maxDepth - depth + MIN_DEPTH);
            int total = 0;
            for (int i = 0; i < iterations; i = i + 1) {
                total = total + check(bottomUp(depth));
            }
            System.out.println(iterations + " trees of depth " + depth + " check: " + total);
        }
        System.out.println("long lived tree of depth " + maxDepth + " check: " + check(longLived));
    }
}
