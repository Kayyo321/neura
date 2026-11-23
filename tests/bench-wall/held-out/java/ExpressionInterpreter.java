// Same algorithm, input, and output as ../../fixtures/expression-interpreter.nra
// (row 14): a tokenizer, a recursive-descent parser that builds a tree of node
// classes, and evaluation through virtual calls against a hash map.
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public final class ExpressionInterpreter {
    private static final int STATEMENT_COUNT = 240;
    private static final int MAX_DEPTH = 5;
    private static final int REPARSE_INTERVAL = 8;
    private static final long MODULUS = 1000003L;
    private static final long MASK = 1073741823L;

    private static final String[] NAMES = {"rate", "count", "base", "limit", "delta", "scale", "width", "depth", "x", "y"};
    private static final String[] OPERATORS = {" + ", " - ", "*", " / ", " % ", " > "};

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

    private enum TokenKind {
        NUMBER, NAME, PLUS, MINUS, STAR, SLASH, PERCENT, LESS, GREATER, EQUAL_EQUAL, ASSIGN, LEFT_PAREN,
        RIGHT_PAREN, COMMA, SEMICOLON, END
    }

    private record Token(TokenKind kind, String text, long value) {}

    private static final class Environment {
        final Map<String, Long> variables = new HashMap<>();

        long read(String name) {
            return variables.getOrDefault(name, 0L);
        }

        void write(String name, long value) {
            variables.put(name, value);
        }
    }

    private abstract static class Node {
        abstract long evaluate(Environment env);

        abstract void render(StringBuilder out);

        abstract int size();
    }

    private static final class NumberNode extends Node {
        final long value;

        NumberNode(long value) {
            this.value = value;
        }

        long evaluate(Environment env) {
            return value;
        }

        void render(StringBuilder out) {
            out.append(value);
        }

        int size() {
            return 1;
        }
    }

    private static final class VariableNode extends Node {
        final String name;

        VariableNode(String name) {
            this.name = name;
        }

        long evaluate(Environment env) {
            return env.read(name);
        }

        void render(StringBuilder out) {
            out.append(name);
        }

        int size() {
            return 1;
        }
    }

    private static final class NegateNode extends Node {
        final Node operand;

        NegateNode(Node operand) {
            this.operand = operand;
        }

        long evaluate(Environment env) {
            return (MODULUS - operand.evaluate(env)) % MODULUS;
        }

        void render(StringBuilder out) {
            out.append("(-");
            operand.render(out);
            out.append(")");
        }

        int size() {
            return 1 + operand.size();
        }
    }

    private static final class BinaryNode extends Node {
        final TokenKind operator;
        final Node left;
        final Node right;

        BinaryNode(TokenKind operator, Node left, Node right) {
            this.operator = operator;
            this.left = left;
            this.right = right;
        }

        long evaluate(Environment env) {
            long a = left.evaluate(env);
            long b = right.evaluate(env);
            return switch (operator) {
                case PLUS -> (a + b) % MODULUS;
                case MINUS -> (a - b + MODULUS) % MODULUS;
                case STAR -> a * b % MODULUS;
                case SLASH -> a / (b + 1);
                case PERCENT -> a % (b + 1);
                case LESS -> a < b ? 1 : 0;
                case GREATER -> a > b ? 1 : 0;
                default -> a == b ? 1 : 0;
            };
        }

        void render(StringBuilder out) {
            out.append("(");
            left.render(out);
            out.append(" ").append(symbol(operator)).append(" ");
            right.render(out);
            out.append(")");
        }

        int size() {
            return 1 + left.size() + right.size();
        }
    }

    private abstract static class CallNode extends Node {
        final String name;
        final List<Node> arguments;

        CallNode(String name, List<Node> arguments) {
            this.name = name;
            this.arguments = arguments;
        }

        void render(StringBuilder out) {
            out.append(name).append("(");
            for (int i = 0; i < arguments.size(); i++) {
                if (i > 0) {
                    out.append(", ");
                }
                arguments.get(i).render(out);
            }
            out.append(")");
        }

        int size() {
            int total = 1;
            for (Node argument : arguments) {
                total += argument.size();
            }
            return total;
        }
    }

    private static final class MinNode extends CallNode {
        MinNode(List<Node> arguments) {
            super("min", arguments);
        }

        long evaluate(Environment env) {
            long a = arguments.get(0).evaluate(env);
            long b = arguments.get(1).evaluate(env);
            return a < b ? a : b;
        }
    }

    private static final class MaxNode extends CallNode {
        MaxNode(List<Node> arguments) {
            super("max", arguments);
        }

        long evaluate(Environment env) {
            long a = arguments.get(0).evaluate(env);
            long b = arguments.get(1).evaluate(env);
            return a > b ? a : b;
        }
    }

    private static final class IfNode extends CallNode {
        IfNode(List<Node> arguments) {
            super("if", arguments);
        }

        long evaluate(Environment env) {
            if (arguments.get(0).evaluate(env) != 0) {
                return arguments.get(1).evaluate(env);
            }
            return arguments.get(2).evaluate(env);
        }
    }

    private record Statement(String target, Node expression) {}

    private static String symbol(TokenKind kind) {
        return switch (kind) {
            case PLUS -> "+";
            case MINUS -> "-";
            case STAR -> "*";
            case SLASH -> "/";
            case PERCENT -> "%";
            case LESS -> "<";
            case GREATER -> ">";
            default -> "==";
        };
    }

    private static boolean isDigit(char c) {
        return c >= '0' && c <= '9';
    }

    private static boolean isLetter(char c) {
        return (c >= 'a' && c <= 'z') || c == '_';
    }

    private static List<Token> tokenize(String source) {
        List<Token> tokens = new ArrayList<>();
        int at = 0;
        while (at < source.length()) {
            char c = source.charAt(at);
            if (c == ' ' || c == '\n') {
                at += 1;
            } else if (isDigit(c)) {
                int start = at;
                long value = 0;
                while (at < source.length() && isDigit(source.charAt(at))) {
                    value = value * 10 + (source.charAt(at) - '0');
                    at += 1;
                }
                tokens.add(new Token(TokenKind.NUMBER, source.substring(start, at), value));
            } else if (isLetter(c)) {
                int start = at;
                while (at < source.length() && isLetter(source.charAt(at))) {
                    at += 1;
                }
                tokens.add(new Token(TokenKind.NAME, source.substring(start, at), 0));
            } else {
                int start = at;
                TokenKind kind;
                if (c == '=' && at + 1 < source.length() && source.charAt(at + 1) == '=') {
                    kind = TokenKind.EQUAL_EQUAL;
                    at += 2;
                } else {
                    at += 1;
                    kind = switch (c) {
                        case '+' -> TokenKind.PLUS;
                        case '-' -> TokenKind.MINUS;
                        case '*' -> TokenKind.STAR;
                        case '/' -> TokenKind.SLASH;
                        case '%' -> TokenKind.PERCENT;
                        case '<' -> TokenKind.LESS;
                        case '>' -> TokenKind.GREATER;
                        case '=' -> TokenKind.ASSIGN;
                        case '(' -> TokenKind.LEFT_PAREN;
                        case ')' -> TokenKind.RIGHT_PAREN;
                        case ',' -> TokenKind.COMMA;
                        case ';' -> TokenKind.SEMICOLON;
                        default -> throw new IllegalStateException("expression interpreter: unexpected character");
                    };
                }
                tokens.add(new Token(kind, source.substring(start, at), 0));
            }
        }
        tokens.add(new Token(TokenKind.END, "", 0));
        return tokens;
    }

    private static final class Parser {
        private final List<Token> tokens;
        private int position;

        Parser(List<Token> tokens) {
            this.tokens = tokens;
        }

        private TokenKind peek() {
            return tokens.get(position).kind();
        }

        private Token advance() {
            Token token = tokens.get(position);
            position += 1;
            return token;
        }

        private Token expect(TokenKind kind) {
            if (peek() != kind) {
                throw new IllegalStateException("expression interpreter: unexpected token " + tokens.get(position).text());
            }
            return advance();
        }

        List<Statement> script() {
            List<Statement> statements = new ArrayList<>();
            while (peek() != TokenKind.END) {
                String target = expect(TokenKind.NAME).text();
                expect(TokenKind.ASSIGN);
                Node expression = comparison();
                expect(TokenKind.SEMICOLON);
                statements.add(new Statement(target, expression));
            }
            return statements;
        }

        private Node comparison() {
            Node left = additive();
            TokenKind kind = peek();
            if (kind == TokenKind.LESS || kind == TokenKind.GREATER || kind == TokenKind.EQUAL_EQUAL) {
                advance();
                return new BinaryNode(kind, left, additive());
            }
            return left;
        }

        private Node additive() {
            Node node = term();
            while (peek() == TokenKind.PLUS || peek() == TokenKind.MINUS) {
                TokenKind kind = advance().kind();
                node = new BinaryNode(kind, node, term());
            }
            return node;
        }

        private Node term() {
            Node node = unary();
            while (peek() == TokenKind.STAR || peek() == TokenKind.SLASH || peek() == TokenKind.PERCENT) {
                TokenKind kind = advance().kind();
                node = new BinaryNode(kind, node, unary());
            }
            return node;
        }

        private Node unary() {
            if (peek() == TokenKind.MINUS) {
                advance();
                return new NegateNode(unary());
            }
            return primary();
        }

        private Node primary() {
            Token token = advance();
            if (token.kind() == TokenKind.NUMBER) {
                return new NumberNode(token.value());
            }
            if (token.kind() == TokenKind.LEFT_PAREN) {
                Node inner = comparison();
                expect(TokenKind.RIGHT_PAREN);
                return inner;
            }
            if (token.kind() != TokenKind.NAME) {
                throw new IllegalStateException("expression interpreter: unexpected token " + token.text());
            }
            if (peek() != TokenKind.LEFT_PAREN) {
                return new VariableNode(token.text());
            }
            advance();
            List<Node> arguments = new ArrayList<>();
            arguments.add(comparison());
            while (peek() == TokenKind.COMMA) {
                advance();
                arguments.add(comparison());
            }
            expect(TokenKind.RIGHT_PAREN);
            if (token.text().equals("min") && arguments.size() == 2) {
                return new MinNode(arguments);
            }
            if (token.text().equals("max") && arguments.size() == 2) {
                return new MaxNode(arguments);
            }
            if (token.text().equals("if") && arguments.size() == 3) {
                return new IfNode(arguments);
            }
            throw new IllegalStateException("expression interpreter: unknown function " + token.text());
        }
    }

    // Appends a random expression whose nesting is at most depth.
    private static void generate(Random random, StringBuilder out, int depth) {
        int choice = depth == 0 ? random.next(2) : random.next(10);
        if (choice == 0) {
            out.append(random.next(1000));
        } else if (choice == 1) {
            out.append(NAMES[random.next(NAMES.length)]);
        } else if (choice == 2) {
            out.append("-");
            generate(random, out, depth - 1);
        } else if (choice == 3) {
            out.append("(");
            generate(random, out, depth - 1);
            out.append(")");
        } else if (choice == 4) {
            out.append(random.next(2) == 0 ? "min(" : "max(");
            generate(random, out, depth - 1);
            out.append(", ");
            generate(random, out, depth - 1);
            out.append(")");
        } else if (choice == 5) {
            out.append("if(");
            generate(random, out, depth - 1);
            out.append(random.next(2) == 0 ? " < " : " == ");
            generate(random, out, depth - 1);
            out.append(", ");
            generate(random, out, depth - 1);
            out.append(", ");
            generate(random, out, depth - 1);
            out.append(")");
        } else {
            out.append("(");
            generate(random, out, depth - 1);
            out.append(OPERATORS[random.next(OPERATORS.length)]);
            generate(random, out, depth - 1);
            out.append(")");
        }
    }

    private static String scriptSource(Random random) {
        StringBuilder out = new StringBuilder();
        for (int i = 0; i < STATEMENT_COUNT; i++) {
            out.append(NAMES[random.next(NAMES.length)]).append(" = ");
            generate(random, out, MAX_DEPTH);
            out.append(";\n");
        }
        return out.toString();
    }

    private static List<Statement> parse(String source) {
        return new Parser(tokenize(source)).script();
    }

    public static void main(String[] args) {
        System.out.print(workload(Integer.parseInt(args[0])));
    }

    public static String workload(int rounds) {
        Random random = new Random(7177);
        String source = scriptSource(random);
        List<Statement> statements = parse(source);
        Environment env = new Environment();
        long checksum = 0;
        for (int round = 0; round < rounds; round++) {
            if (round % REPARSE_INTERVAL == REPARSE_INTERVAL - 1) {
                statements = parse(source);
            }
            for (String name : NAMES) {
                env.write(name, random.next((int) MODULUS));
            }
            for (Statement statement : statements) {
                long value = statement.expression().evaluate(env);
                env.write(statement.target(), value);
                checksum = (checksum * 31 + value) & MASK;
            }
        }
        StringBuilder rendered = new StringBuilder();
        int nodes = 0;
        for (Statement statement : statements) {
            rendered.append(statement.target()).append(" = ");
            statement.expression().render(rendered);
            rendered.append(";\n");
            nodes += statement.expression().size();
        }
        long textChecksum = 0;
        for (byte b : rendered.toString().getBytes(StandardCharsets.UTF_8)) {
            textChecksum = (textChecksum * 31 + (b & 0xff)) & MASK;
        }
        return "expression-interpreter " + rounds + " " + statements.size() + " " + nodes + " "
            + source.getBytes(StandardCharsets.UTF_8).length + " " + checksum + " " + textChecksum + "\n";
    }
}
