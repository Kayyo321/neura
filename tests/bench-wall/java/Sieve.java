// Same algorithm, input, and output as ../neura/sieve.nra.
public final class Sieve {
    public static void main(String[] args) {
        int n = Integer.parseInt(args[0]);
        boolean[] composite = new boolean[n + 1];
        int count = 0;
        long sum = 0;
        for (int i = 2; i <= n; i = i + 1) {
            if (!composite[i]) {
                count = count + 1;
                sum = sum + i;
                long multiple = (long) i * (long) i;
                while (multiple <= n) {
                    composite[(int) multiple] = true;
                    multiple = multiple + i;
                }
            }
        }
        System.out.println("sieve " + count + " " + sum);
    }
}
