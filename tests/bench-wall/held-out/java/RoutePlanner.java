// Same algorithm, input, and output as ../../fixtures/route-planner.nra (row 15):
// adjacency lists of road objects, Dijkstra's algorithm with a hand-written
// binary heap that stops at the destination, and path reconstruction. The
// heap and the per-intersection tables are primitive arrays, as idiomatic
// Java keeps them; Neura keeps them in lists.
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;

public final class RoutePlanner {
    private static final int WIDTH = 110;
    private static final int HEIGHT = 90;
    private static final int HIGHWAYS = 900;
    private static final int CLOSURE_INTERVAL = 64;
    private static final long UNREACHED = Long.MAX_VALUE;
    private static final long MASK = 1073741823L;

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

    private static final class Road {
        final int target;
        final int minutes;
        boolean open = true;

        Road(int target, int minutes) {
            this.target = target;
            this.minutes = minutes;
        }
    }

    private static final class Intersection {
        final int id;
        final List<Road> roads = new ArrayList<>();

        Intersection(int id) {
            this.id = id;
        }
    }

    private static final class RoadNetwork {
        final List<Intersection> intersections = new ArrayList<>();
        final List<Road> allRoads = new ArrayList<>();

        RoadNetwork(Random random) {
            for (int i = 0; i < WIDTH * HEIGHT; i++) {
                intersections.add(new Intersection(i));
            }
            for (int y = 0; y < HEIGHT; y++) {
                for (int x = 0; x < WIDTH; x++) {
                    int here = y * WIDTH + x;
                    if (x + 1 < WIDTH && random.next(10) != 0) {
                        connect(here, here + 1, 5 + random.next(40));
                    }
                    if (y + 1 < HEIGHT && random.next(10) != 0) {
                        connect(here, here + WIDTH, 5 + random.next(40));
                    }
                }
            }
            for (int i = 0; i < HIGHWAYS; i++) {
                int from = random.next(intersections.size());
                int to = random.next(intersections.size());
                if (from != to) {
                    connect(from, to, 60 + random.next(400));
                }
            }
        }

        private void connect(int from, int to, int minutes) {
            Road forward = new Road(to, minutes);
            Road backward = new Road(from, minutes);
            intersections.get(from).roads.add(forward);
            intersections.get(to).roads.add(backward);
            allRoads.add(forward);
            allRoads.add(backward);
        }

        int size() {
            return intersections.size();
        }
    }

    // A binary min-heap of (distance, intersection) pairs in two parallel arrays.
    private static final class MinHeap {
        private long[] keys = new long[64];
        private int[] values = new int[64];
        private int size;

        boolean isEmpty() {
            return size == 0;
        }

        void clear() {
            size = 0;
        }

        long topKey() {
            return keys[0];
        }

        void push(long key, int value) {
            if (size == keys.length) {
                keys = Arrays.copyOf(keys, size * 2);
                values = Arrays.copyOf(values, size * 2);
            }
            keys[size] = key;
            values[size] = value;
            int child = size;
            size += 1;
            while (child > 0) {
                int parent = (child - 1) / 2;
                if (keys[parent] <= keys[child]) {
                    break;
                }
                swap(parent, child);
                child = parent;
            }
        }

        int pop() {
            int top = values[0];
            size -= 1;
            keys[0] = keys[size];
            values[0] = values[size];
            int parent = 0;
            while (true) {
                int left = parent * 2 + 1;
                if (left >= size) {
                    break;
                }
                int smallest = left;
                if (left + 1 < size && keys[left + 1] < keys[left]) {
                    smallest = left + 1;
                }
                if (keys[parent] <= keys[smallest]) {
                    break;
                }
                swap(parent, smallest);
                parent = smallest;
            }
            return top;
        }

        private void swap(int a, int b) {
            long key = keys[a];
            keys[a] = keys[b];
            keys[b] = key;
            int value = values[a];
            values[a] = values[b];
            values[b] = value;
        }
    }

    private record Route(long minutes, List<Integer> path, int settled) {}

    private static final class Planner {
        private final RoadNetwork network;
        private final long[] distance;
        private final int[] previous;
        private final int[] stamp;
        private final MinHeap heap = new MinHeap();
        private int query;

        Planner(RoadNetwork roads) {
            network = roads;
            distance = new long[roads.size()];
            previous = new int[roads.size()];
            stamp = new int[roads.size()];
            Arrays.fill(distance, UNREACHED);
            Arrays.fill(previous, -1);
            Arrays.fill(stamp, -1);
        }

        private long distanceOf(int node) {
            return stamp[node] == query ? distance[node] : UNREACHED;
        }

        private void reach(int node, long minutes, int from) {
            stamp[node] = query;
            distance[node] = minutes;
            previous[node] = from;
            heap.push(minutes, node);
        }

        Route plan(int source, int target) {
            query += 1;
            heap.clear();
            reach(source, 0, -1);
            int settled = 0;
            while (!heap.isEmpty()) {
                long minutes = heap.topKey();
                int node = heap.pop();
                if (minutes > distanceOf(node)) {
                    continue;
                }
                settled += 1;
                if (node == target) {
                    break;
                }
                for (Road road : network.intersections.get(node).roads) {
                    if (!road.open) {
                        continue;
                    }
                    long candidate = minutes + road.minutes;
                    if (candidate < distanceOf(road.target)) {
                        reach(road.target, candidate, node);
                    }
                }
            }
            List<Integer> path = new ArrayList<>();
            long total = distanceOf(target);
            if (total != UNREACHED) {
                for (int at = target; at != -1; at = previous[at]) {
                    path.add(at);
                }
                Collections.reverse(path);
            }
            return new Route(total, path, settled);
        }
    }

    public static void main(String[] args) {
        System.out.print(workload(Integer.parseInt(args[0])));
    }

    public static String workload(int queries) {
        Random random = new Random(424242);
        RoadNetwork network = new RoadNetwork(random);
        Planner planner = new Planner(network);
        Road closed = network.allRoads.get(0);
        int reached = 0;
        long hops = 0;
        long settled = 0;
        long checksum = 0;
        for (int i = 0; i < queries; i++) {
            if (i % CLOSURE_INTERVAL == 0) {
                closed.open = true;
                closed = network.allRoads.get(random.next(network.allRoads.size()));
                closed.open = false;
            }
            Route route = planner.plan(random.next(network.size()), random.next(network.size()));
            settled += route.settled();
            if (route.path().isEmpty()) {
                checksum = (checksum * 31 + 7) & MASK;
                continue;
            }
            reached += 1;
            hops += route.path().size() - 1;
            checksum = (checksum * 31 + route.minutes()) & MASK;
            for (int node : route.path()) {
                checksum = (checksum * 17 + node) & MASK;
            }
        }
        return "route-planner " + queries + " " + network.allRoads.size() + " " + reached + " " + hops + " "
            + settled + " " + checksum + "\n";
    }
}
