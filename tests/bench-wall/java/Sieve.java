// Same algorithm, input, and output as ../neura/sieve.nra.
public final class Sieve {
    public static void main(String[] args) {
        System.out.print(workload(Integer.parseInt(args[0])));
    }

    public static String workload(int n) {
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
        return "sieve " + count + " " + sum + System.lineSeparator();
    }
}
