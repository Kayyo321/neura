// Same algorithm, input, and output as ../../fixtures/order-ledger.nra (row 13).
// Std.Collections.HashMap is the counterpart of java.util.HashMap here: the
// grouped totals are sorted by key before they are reported, so no map order
// is observed.
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

public final class OrderLedger {
    private static final int CUSTOMER_COUNT = 4000;
    private static final int PRODUCT_COUNT = 600;
    private static final int TOP_CUSTOMERS = 40;
    private static final long MASK = 1073741823L;

    private static final String[] REGIONS = {"north", "south", "east", "west", "central", "coast", "valley", "highland"};
    private static final String[] CATEGORIES = {
        "tools", "garden", "kitchen", "books", "toys", "audio", "office", "sport", "paint", "lighting"
    };

    private static final class Random {
        private long state;

        Random(long seed) {
            state = seed;
        }

        int next(int bound) {
            state = (state * 1103515245L + 12345L) & 2147483647L;
            return (int) ((state >> 4) % bound);
        }
    }

    private record Product(String sku, String category, long unitCents) {}

    private static final class Customer {
        final int id;
        final String region;
        final int tier;
        long spentCents;
        int orderCount;

        Customer(int id, String region, int tier) {
            this.id = id;
            this.region = region;
            this.tier = tier;
        }
    }

    private record OrderLine(Product product, int quantity) {}

    private static final class Order {
        final int id;
        final Customer customer;
        final int day;
        final List<OrderLine> lines = new ArrayList<>();

        Order(int id, Customer customer, int day) {
            this.id = id;
            this.customer = customer;
            this.day = day;
        }

        long subtotal() {
            long sum = 0;
            for (OrderLine line : lines) {
                sum += line.product().unitCents() * line.quantity();
            }
            return sum;
        }

        int units() {
            int count = 0;
            for (OrderLine line : lines) {
                count += line.quantity();
            }
            return count;
        }
    }

    private interface PricingRule {
        // The discount in cents; a negative value is a surcharge.
        long discount(Order order, long subtotal);
    }

    private static final class TierDiscount implements PricingRule {
        public long discount(Order order, long subtotal) {
            return subtotal * order.customer.tier * 2 / 100;
        }
    }

    private static final class BulkDiscount implements PricingRule {
        private final int threshold;

        BulkDiscount(int threshold) {
            this.threshold = threshold;
        }

        public long discount(Order order, long subtotal) {
            return order.units() >= threshold ? subtotal / 20 : 0;
        }
    }

    private static final class WeekendSurcharge implements PricingRule {
        public long discount(Order order, long subtotal) {
            int weekday = order.day % 7;
            return weekday == 5 || weekday == 6 ? -(subtotal / 50) : 0;
        }
    }

    private static final class Totals {
        final String key;
        int orders;
        long units;
        long revenueCents;
        long discountCents;

        Totals(String key) {
            this.key = key;
        }

        void add(long orderUnits, long revenue, long discount) {
            orders += 1;
            units += orderUnits;
            revenueCents += revenue;
            discountCents += discount;
        }
    }

    public static void main(String[] args) {
        System.out.print(workload(Integer.parseInt(args[0])));
    }

    private static Totals totalsFor(Map<String, Totals> table, String key) {
        return table.computeIfAbsent(key, Totals::new);
    }

    private static List<Product> catalogue(Random random) {
        List<Product> products = new ArrayList<>();
        for (int i = 0; i < PRODUCT_COUNT; i++) {
            String category = CATEGORIES[random.next(CATEGORIES.length)];
            String sku = category.substring(0, 3).toUpperCase(Locale.ROOT) + "-" + (1000 + i);
            products.add(new Product(sku, category, 199 + (long) random.next(4800) * 10));
        }
        return products;
    }

    private static List<Customer> customers(Random random) {
        List<Customer> people = new ArrayList<>();
        for (int i = 0; i < CUSTOMER_COUNT; i++) {
            people.add(new Customer(i, REGIONS[random.next(REGIONS.length)], random.next(4)));
        }
        return people;
    }

    private static List<Order> placeOrders(Random random, int count, List<Customer> buyers, List<Product> products) {
        List<Order> orders = new ArrayList<>();
        for (int i = 0; i < count; i++) {
            Order order = new Order(i, buyers.get(random.next(buyers.size())), i / 500);
            int lineCount = 1 + random.next(6);
            for (int j = 0; j < lineCount; j++) {
                order.lines.add(new OrderLine(products.get(random.next(products.size())), 1 + random.next(9)));
            }
            orders.add(order);
        }
        return orders;
    }

    private static List<Totals> sortedTotals(Map<String, Totals> table) {
        List<Totals> values = new ArrayList<>(table.values());
        values.sort(Comparator.comparing(totals -> totals.key));
        return values;
    }

    private static void reportSection(StringBuilder report, String title, List<Totals> rows) {
        report.append("== ").append(title).append(" (").append(rows.size()).append(")\n");
        for (Totals row : rows) {
            long average = row.revenueCents / row.orders;
            report.append(String.format(Locale.ROOT, "%-20s%8d%10d%14d%12d%10d\n", row.key, row.orders, row.units,
                row.revenueCents, row.discountCents, average));
        }
    }

    private static long fold(long checksum, String text) {
        long value = checksum;
        for (byte b : text.getBytes(StandardCharsets.UTF_8)) {
            value = (value * 31 + (b & 0xff)) & MASK;
        }
        return value;
    }

    public static String workload(int orderCount) {
        Random random = new Random(20261123);
        List<Product> products = catalogue(random);
        List<Customer> buyers = customers(random);
        List<Order> orders = placeOrders(random, orderCount, buyers, products);
        List<PricingRule> rules = List.of(new TierDiscount(), new BulkDiscount(24), new WeekendSurcharge());

        Map<String, Totals> byRegion = new HashMap<>();
        Map<String, Totals> byCategory = new HashMap<>();
        Map<String, Totals> byPair = new HashMap<>();
        long ledgerCents = 0;
        for (Order order : orders) {
            long subtotal = order.subtotal();
            long discount = 0;
            for (PricingRule rule : rules) {
                discount += rule.discount(order, subtotal);
            }
            long total = subtotal - discount;
            Customer customer = order.customer;
            customer.spentCents += total;
            customer.orderCount += 1;
            ledgerCents += total;
            totalsFor(byRegion, customer.region).add(order.units(), total, discount);
            for (OrderLine line : order.lines) {
                long revenue = line.product().unitCents() * line.quantity();
                String category = line.product().category();
                totalsFor(byCategory, category).add(line.quantity(), revenue, 0);
                totalsFor(byPair, customer.region + "/" + category).add(line.quantity(), revenue, 0);
            }
        }

        List<Customer> ranked = new ArrayList<>(buyers);
        ranked.sort(Comparator.comparingLong((Customer c) -> c.spentCents).reversed()
            .thenComparingInt(c -> c.id));
        StringBuilder report = new StringBuilder();
        report.append("ledger ").append(orders.size()).append(" orders ").append(ledgerCents).append(" cents\n");
        report.append("== top ").append(TOP_CUSTOMERS).append(" customers\n");
        for (int i = 0; i < TOP_CUSTOMERS && i < ranked.size(); i++) {
            Customer customer = ranked.get(i);
            report.append(String.format(Locale.ROOT, "%4d C%05d %-10s%6d%14d\n", i + 1, customer.id, customer.region,
                customer.orderCount, customer.spentCents));
        }
        reportSection(report, "region", sortedTotals(byRegion));
        reportSection(report, "category", sortedTotals(byCategory));
        reportSection(report, "region/category", sortedTotals(byPair));
        String text = report.toString();
        int bytes = text.getBytes(StandardCharsets.UTF_8).length;
        return "order-ledger " + orderCount + " " + bytes + " " + fold(0, text) + "\n";
    }
}
