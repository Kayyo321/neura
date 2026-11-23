// Row 13. Pricing, grouping, ranking, report formatting and checksum match order-ledger.nra.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

public static class OrderLedger
{
    private const int CustomerCount = 4000;
    private const int ProductCount = 600;
    private const int TopCustomers = 40;
    private const long Mask = 1073741823;
    private static readonly CultureInfo Culture = CultureInfo.InvariantCulture;
    private static readonly string[] Regions = { "north", "south", "east", "west", "central", "coast", "valley", "highland" };
    private static readonly string[] Categories = {
        "tools", "garden", "kitchen", "books", "toys", "audio", "office", "sport", "paint", "lighting"
    };

    private sealed class RandomSource(long seed)
    {
        private long state = seed;

        public int Next(int bound)
        {
            state = (state * 1103515245 + 12345) & 2147483647;
            return (int)((state >> 4) % bound);
        }
    }

    private sealed record Product(string Sku, string Category, long UnitCents);
    private sealed record OrderLine(Product Product, int Quantity);

    private sealed class Customer(int id, string region, int tier)
    {
        public readonly int Id = id;
        public readonly string Region = region;
        public readonly int Tier = tier;
        public long SpentCents;
        public int OrderCount;
    }

    private sealed class Order(int id, Customer customer, int day)
    {
        public readonly int Id = id;
        public readonly Customer Customer = customer;
        public readonly int Day = day;
        public readonly List<OrderLine> Lines = new();

        public long Subtotal()
        {
            long sum = 0;
            foreach (var line in Lines)
                sum += line.Product.UnitCents * line.Quantity;
            return sum;
        }

        public int Units()
        {
            int count = 0;
            foreach (var line in Lines)
                count += line.Quantity;
            return count;
        }
    }

    private interface IPricingRule
    {
        long Discount(Order order, long subtotal);
    }

    private sealed class TierDiscount : IPricingRule
    {
        public long Discount(Order order, long subtotal) => subtotal * order.Customer.Tier * 2 / 100;
    }

    private sealed class BulkDiscount(int threshold) : IPricingRule
    {
        public long Discount(Order order, long subtotal) => order.Units() >= threshold ? subtotal / 20 : 0;
    }

    private sealed class WeekendSurcharge : IPricingRule
    {
        public long Discount(Order order, long subtotal) => order.Day % 7 is 5 or 6 ? -(subtotal / 50) : 0;
    }

    private sealed class Totals(string key)
    {
        public readonly string Key = key;
        public int Orders;
        public long Units;
        public long RevenueCents;
        public long DiscountCents;

        public void Add(long orderUnits, long revenue, long discount)
        {
            Orders += 1;
            Units += orderUnits;
            RevenueCents += revenue;
            DiscountCents += discount;
        }
    }

    private static Totals TotalsFor(Dictionary<string, Totals> table, string key)
    {
        if (!table.TryGetValue(key, out var value))
        {
            value = new Totals(key);
            table.Add(key, value);
        }
        return value;
    }

    private static List<Product> Catalogue(RandomSource random)
    {
        var products = new List<Product>();
        for (int i = 0; i < ProductCount; i++)
        {
            string category = Categories[random.Next(Categories.Length)];
            string sku = category[..3].ToUpperInvariant() + "-" + (1000 + i).ToString(Culture);
            products.Add(new Product(sku, category, 199 + (long)random.Next(4800) * 10));
        }
        return products;
    }

    private static List<Customer> Customers(RandomSource random)
    {
        var people = new List<Customer>();
        for (int i = 0; i < CustomerCount; i++)
            people.Add(new Customer(i, Regions[random.Next(Regions.Length)], random.Next(4)));
        return people;
    }

    private static List<Order> PlaceOrders(RandomSource random, int count, List<Customer> buyers, List<Product> products)
    {
        var orders = new List<Order>();
        for (int i = 0; i < count; i++)
        {
            var order = new Order(i, buyers[random.Next(buyers.Count)], i / 500);
            int lineCount = 1 + random.Next(6);
            for (int j = 0; j < lineCount; j++)
                order.Lines.Add(new OrderLine(products[random.Next(products.Count)], 1 + random.Next(9)));
            orders.Add(order);
        }
        return orders;
    }

    private static void ReportSection(StringBuilder report, string title, Dictionary<string, Totals> table)
    {
        var rows = new List<Totals>(table.Values);
        rows.Sort((a, b) => string.CompareOrdinal(a.Key, b.Key));
        report.Append(Culture, $"== {title} ({rows.Count})\n");
        foreach (var row in rows)
        {
            long average = row.RevenueCents / row.Orders;
            report.Append(Culture, $"{row.Key,-20}{row.Orders,8}{row.Units,10}{row.RevenueCents,14}{row.DiscountCents,12}{average,10}\n");
        }
    }

    public static string Workload(int orderCount)
    {
        var random = new RandomSource(20261123);
        var products = Catalogue(random);
        var buyers = Customers(random);
        var orders = PlaceOrders(random, orderCount, buyers, products);
        var rules = new List<IPricingRule> { new TierDiscount(), new BulkDiscount(24), new WeekendSurcharge() };
        var byRegion = new Dictionary<string, Totals>();
        var byCategory = new Dictionary<string, Totals>();
        var byPair = new Dictionary<string, Totals>();
        long ledgerCents = 0;
        foreach (var order in orders)
        {
            long subtotal = order.Subtotal();
            long discount = 0;
            foreach (var rule in rules)
                discount += rule.Discount(order, subtotal);
            long total = subtotal - discount;
            var customer = order.Customer;
            customer.SpentCents += total;
            customer.OrderCount++;
            ledgerCents += total;
            TotalsFor(byRegion, customer.Region).Add(order.Units(), total, discount);
            foreach (var line in order.Lines)
            {
                long revenue = line.Product.UnitCents * line.Quantity;
                string category = line.Product.Category;
                TotalsFor(byCategory, category).Add(line.Quantity, revenue, 0);
                TotalsFor(byPair, customer.Region + "/" + category).Add(line.Quantity, revenue, 0);
            }
        }

        var ranked = new List<Customer>(buyers);
        ranked.Sort((a, b) => {
            int order = b.SpentCents.CompareTo(a.SpentCents);
            return order != 0 ? order : a.Id.CompareTo(b.Id);
        });
        var report = new StringBuilder();
        report.Append(Culture, $"ledger {orders.Count} orders {ledgerCents} cents\n");
        report.Append(Culture, $"== top {TopCustomers} customers\n");
        for (int i = 0; i < TopCustomers && i < ranked.Count; i++)
        {
            var c = ranked[i];
            report.Append(Culture, $"{i + 1,4} C{c.Id:D5} {c.Region,-10}{c.OrderCount,6}{c.SpentCents,14}\n");
        }
        ReportSection(report, "region", byRegion);
        ReportSection(report, "category", byCategory);
        ReportSection(report, "region/category", byPair);
        byte[] text = Encoding.UTF8.GetBytes(report.ToString());
        long checksum = 0;
        foreach (byte b in text)
            checksum = (checksum * 31 + b) & Mask;
        return FormattableString.Invariant($"order-ledger {orderCount} {text.Length} {checksum}\n");
    }

    public static int Main(string[] args)
    {
        if (args.Length != 1 || !int.TryParse(args[0], NumberStyles.AllowLeadingSign, Culture, out int size) || size < 0)
            return 1;
        Console.Write(Workload(size));
        return 0;
    }
}
