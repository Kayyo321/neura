// Deterministic twin of the expression-interpreter workload.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

public static class ExpressionInterpreter
{
    private const int StatementCount = 240;
    private const int MaxDepth = 5;
    private const int ChainDepth = 32;
    private const int ReparseInterval = 8;
    private const long Modulus = 1000003L;
    private const long Mask = 1073741823L;

    private static readonly string[] Names = { "rate", "count", "base", "limit", "delta", "scale", "width", "depth", "x", "y" };
    private static readonly string[] Operators = { " + ", " - ", "*", " / ", " % ", " > " };

    private sealed class Random
    {
        private long state;

        public Random(long seed)
        {
            state = seed;
        }

        public int Next(int bound)
        {
            state = (state * 1103515245L + 12345L) & 2147483647L;
            return (int)((state >> 4) % bound);
        }
    }

    private enum TokenKind
    {
        Number, Name, Plus, Minus, Star, Slash, Percent, Less, Greater, EqualEqual, Assign, LeftParen,
        RightParen, Comma, Semicolon, End
    }

    private sealed record Token(TokenKind Kind, string Text, long Value);

    private sealed class Environment
    {
        private readonly Dictionary<string, long> variables = new(StringComparer.Ordinal);

        public long Read(string name)
        {
            return variables.GetValueOrDefault(name);
        }

        public void Write(string name, long value)
        {
            variables[name] = value;
        }
    }

    private abstract class Node
    {
        public abstract long Evaluate(Environment env);

        public abstract void Render(StringBuilder builder);

        public abstract int Size();
    }

    private sealed class NumberNode : Node
    {
        private readonly long value;

        public NumberNode(long value)
        {
            this.value = value;
        }

        public override long Evaluate(Environment env)
        {
            return value;
        }

        public override void Render(StringBuilder builder)
        {
            builder.Append(value.ToString(CultureInfo.InvariantCulture));
        }

        public override int Size()
        {
            return 1;
        }
    }

    private sealed class VariableNode : Node
    {
        private readonly string name;

        public VariableNode(string name)
        {
            this.name = name;
        }

        public override long Evaluate(Environment env)
        {
            return env.Read(name);
        }

        public override void Render(StringBuilder builder)
        {
            builder.Append(name);
        }

        public override int Size()
        {
            return 1;
        }
    }

    private sealed class NegateNode : Node
    {
        private readonly Node operand;

        public NegateNode(Node operand)
        {
            this.operand = operand;
        }

        public override long Evaluate(Environment env)
        {
            return (Modulus - operand.Evaluate(env)) % Modulus;
        }

        public override void Render(StringBuilder builder)
        {
            builder.Append("(-");
            operand.Render(builder);
            builder.Append(")");
        }

        public override int Size()
        {
            return 1 + operand.Size();
        }
    }

    private abstract class BinaryNode : Node
    {
        private readonly string symbol;
        protected readonly Node left;
        protected readonly Node right;

        protected BinaryNode(string symbol, Node left, Node right)
        {
            this.symbol = symbol;
            this.left = left;
            this.right = right;
        }

        public override void Render(StringBuilder builder)
        {
            builder.Append("(");
            left.Render(builder);
            builder.Append(" ").Append(symbol).Append(" ");
            right.Render(builder);
            builder.Append(")");
        }

        public override int Size()
        {
            return 1 + left.Size() + right.Size();
        }
    }

    private sealed class AddNode : BinaryNode
    {
        public AddNode(Node left, Node right) : base("+", left, right) { }

        public override long Evaluate(Environment env)
        {
            long a = left.Evaluate(env);
            long b = right.Evaluate(env);
            return (a + b) % Modulus;
        }
    }

    private sealed class SubtractNode : BinaryNode
    {
        public SubtractNode(Node left, Node right) : base("-", left, right) { }

        public override long Evaluate(Environment env)
        {
            long a = left.Evaluate(env);
            long b = right.Evaluate(env);
            return (a - b + Modulus) % Modulus;
        }
    }

    private sealed class MultiplyNode : BinaryNode
    {
        public MultiplyNode(Node left, Node right) : base("*", left, right) { }

        public override long Evaluate(Environment env)
        {
            long a = left.Evaluate(env);
            long b = right.Evaluate(env);
            return a * b % Modulus;
        }
    }

    private sealed class DivideNode : BinaryNode
    {
        public DivideNode(Node left, Node right) : base("/", left, right) { }

        public override long Evaluate(Environment env)
        {
            long a = left.Evaluate(env);
            long b = right.Evaluate(env);
            return a / (b + 1);
        }
    }

    private sealed class RemainderNode : BinaryNode
    {
        public RemainderNode(Node left, Node right) : base("%", left, right) { }

        public override long Evaluate(Environment env)
        {
            long a = left.Evaluate(env);
            long b = right.Evaluate(env);
            return a % (b + 1);
        }
    }

    private sealed class LessNode : BinaryNode
    {
        public LessNode(Node left, Node right) : base("<", left, right) { }

        public override long Evaluate(Environment env)
        {
            long a = left.Evaluate(env);
            long b = right.Evaluate(env);
            return a < b ? 1 : 0;
        }
    }

    private sealed class GreaterNode : BinaryNode
    {
        public GreaterNode(Node left, Node right) : base(">", left, right) { }

        public override long Evaluate(Environment env)
        {
            long a = left.Evaluate(env);
            long b = right.Evaluate(env);
            return a > b ? 1 : 0;
        }
    }

    private sealed class EqualNode : BinaryNode
    {
        public EqualNode(Node left, Node right) : base("==", left, right) { }

        public override long Evaluate(Environment env)
        {
            long a = left.Evaluate(env);
            long b = right.Evaluate(env);
            return a == b ? 1 : 0;
        }
    }

    private abstract class CallNode : Node
    {
        private readonly string name;
        protected readonly List<Node> arguments;

        protected CallNode(string name, List<Node> arguments)
        {
            this.name = name;
            this.arguments = arguments;
        }

        public override void Render(StringBuilder builder)
        {
            builder.Append(name).Append("(");
            for (int i = 0; i < arguments.Count; i++)
            {
                if (i > 0)
                {
                    builder.Append(", ");
                }
                arguments[i].Render(builder);
            }
            builder.Append(")");
        }

        public override int Size()
        {
            int total = 1;
            foreach (Node argument in arguments)
            {
                total += argument.Size();
            }
            return total;
        }
    }

    private sealed class MinNode : CallNode
    {
        public MinNode(List<Node> arguments) : base("min", arguments) { }

        public override long Evaluate(Environment env)
        {
            long a = arguments[0].Evaluate(env);
            long b = arguments[1].Evaluate(env);
            return a < b ? a : b;
        }
    }

    private sealed class MaxNode : CallNode
    {
        public MaxNode(List<Node> arguments) : base("max", arguments) { }

        public override long Evaluate(Environment env)
        {
            long a = arguments[0].Evaluate(env);
            long b = arguments[1].Evaluate(env);
            return a > b ? a : b;
        }
    }

    private sealed class IfNode : CallNode
    {
        public IfNode(List<Node> arguments) : base("if", arguments) { }

        public override long Evaluate(Environment env)
        {
            if (arguments[0].Evaluate(env) != 0)
            {
                return arguments[1].Evaluate(env);
            }
            return arguments[2].Evaluate(env);
        }
    }

    private sealed class SquareNode : CallNode
    {
        public SquareNode(List<Node> arguments) : base("square", arguments) { }

        public override long Evaluate(Environment env)
        {
            long value = arguments[0].Evaluate(env);
            return value * value % Modulus;
        }
    }

    private sealed record Statement(string Target, Node Expression);

    private static Node Binary(TokenKind kind, Node left, Node right)
    {
        return kind switch
        {
            TokenKind.Plus => new AddNode(left, right),
            TokenKind.Minus => new SubtractNode(left, right),
            TokenKind.Star => new MultiplyNode(left, right),
            TokenKind.Slash => new DivideNode(left, right),
            TokenKind.Percent => new RemainderNode(left, right),
            TokenKind.Less => new LessNode(left, right),
            TokenKind.Greater => new GreaterNode(left, right),
            TokenKind.EqualEqual => new EqualNode(left, right),
            _ => throw new InvalidOperationException("expression interpreter: unknown binary operator"),
        };
    }

    private static bool IsDigit(char c)
    {
        return c >= '0' && c <= '9';
    }

    private static bool IsLetter(char c)
    {
        return (c >= 'a' && c <= 'z') || c == '_';
    }

    private static List<Token> Tokenize(string source)
    {
        List<Token> tokens = new();
        int at = 0;
        while (at < source.Length)
        {
            char c = source[at];
            if (c == ' ' || c == '\n')
            {
                at += 1;
            }
            else if (IsDigit(c))
            {
                int start = at;
                long value = 0;
                while (at < source.Length && IsDigit(source[at]))
                {
                    value = value * 10 + (source[at] - '0');
                    at += 1;
                }
                tokens.Add(new Token(TokenKind.Number, source[start..at], value));
            }
            else if (IsLetter(c))
            {
                int start = at;
                while (at < source.Length && IsLetter(source[at]))
                {
                    at += 1;
                }
                tokens.Add(new Token(TokenKind.Name, source[start..at], 0));
            }
            else
            {
                int start = at;
                TokenKind kind;
                if (c == '=' && at + 1 < source.Length && source[at + 1] == '=')
                {
                    kind = TokenKind.EqualEqual;
                    at += 2;
                }
                else
                {
                    at += 1;
                    kind = c switch
                    {
                        '+' => TokenKind.Plus,
                        '-' => TokenKind.Minus,
                        '*' => TokenKind.Star,
                        '/' => TokenKind.Slash,
                        '%' => TokenKind.Percent,
                        '<' => TokenKind.Less,
                        '>' => TokenKind.Greater,
                        '=' => TokenKind.Assign,
                        '(' => TokenKind.LeftParen,
                        ')' => TokenKind.RightParen,
                        ',' => TokenKind.Comma,
                        ';' => TokenKind.Semicolon,
                        _ => throw new InvalidOperationException("expression interpreter: unexpected character"),
                    };
                }
                tokens.Add(new Token(kind, source[start..at], 0));
            }
        }
        tokens.Add(new Token(TokenKind.End, "", 0));
        return tokens;
    }

    private sealed class Parser
    {
        private readonly List<Token> tokens;
        private int position;

        public Parser(List<Token> tokens)
        {
            this.tokens = tokens;
        }

        private TokenKind Peek()
        {
            return tokens[position].Kind;
        }

        private Token Advance()
        {
            Token token = tokens[position];
            position += 1;
            return token;
        }

        private Token Expect(TokenKind kind)
        {
            if (Peek() != kind)
            {
                throw new InvalidOperationException("expression interpreter: unexpected token " + tokens[position].Text);
            }
            return Advance();
        }

        public List<Statement> Script()
        {
            List<Statement> statements = new();
            while (Peek() != TokenKind.End)
            {
                string target = Expect(TokenKind.Name).Text;
                Expect(TokenKind.Assign);
                Node expression = Comparison();
                Expect(TokenKind.Semicolon);
                statements.Add(new Statement(target, expression));
            }
            return statements;
        }

        private Node Comparison()
        {
            Node left = Additive();
            TokenKind kind = Peek();
            if (kind == TokenKind.Less || kind == TokenKind.Greater || kind == TokenKind.EqualEqual)
            {
                Advance();
                return Binary(kind, left, Additive());
            }
            return left;
        }

        private Node Additive()
        {
            Node node = Term();
            while (Peek() == TokenKind.Plus || Peek() == TokenKind.Minus)
            {
                TokenKind kind = Advance().Kind;
                node = Binary(kind, node, Term());
            }
            return node;
        }

        private Node Term()
        {
            Node node = Unary();
            while (Peek() == TokenKind.Star || Peek() == TokenKind.Slash || Peek() == TokenKind.Percent)
            {
                TokenKind kind = Advance().Kind;
                node = Binary(kind, node, Unary());
            }
            return node;
        }

        private Node Unary()
        {
            if (Peek() == TokenKind.Minus)
            {
                Advance();
                return new NegateNode(Unary());
            }
            return Primary();
        }

        private Node Primary()
        {
            Token token = Advance();
            if (token.Kind == TokenKind.Number)
            {
                return new NumberNode(token.Value);
            }
            if (token.Kind == TokenKind.LeftParen)
            {
                Node inner = Comparison();
                Expect(TokenKind.RightParen);
                return inner;
            }
            if (token.Kind != TokenKind.Name)
            {
                throw new InvalidOperationException("expression interpreter: unexpected token " + token.Text);
            }
            if (Peek() != TokenKind.LeftParen)
            {
                return new VariableNode(token.Text);
            }
            Advance();
            List<Node> arguments = new();
            arguments.Add(Comparison());
            while (Peek() == TokenKind.Comma)
            {
                Advance();
                arguments.Add(Comparison());
            }
            Expect(TokenKind.RightParen);
            if (token.Text == "square" && arguments.Count == 1)
            {
                return new SquareNode(arguments);
            }
            if (token.Text == "min" && arguments.Count == 2)
            {
                return new MinNode(arguments);
            }
            if (token.Text == "max" && arguments.Count == 2)
            {
                return new MaxNode(arguments);
            }
            if (token.Text == "if" && arguments.Count == 3)
            {
                return new IfNode(arguments);
            }
            throw new InvalidOperationException("expression interpreter: unknown function " + token.Text);
        }
    }

    // Appends a random expression whose nesting is at most depth.
    private static void Generate(Random random, StringBuilder builder, int depth)
    {
        int choice = depth == 0 ? random.Next(2) : random.Next(10);
        if (choice == 0)
        {
            builder.Append(random.Next(1000).ToString(CultureInfo.InvariantCulture));
        }
        else if (choice == 1)
        {
            builder.Append(Names[random.Next(Names.Length)]);
        }
        else if (choice == 2)
        {
            builder.Append("-");
            Generate(random, builder, depth - 1);
        }
        else if (choice == 3)
        {
            builder.Append("(");
            Generate(random, builder, depth - 1);
            builder.Append(")");
        }
        else if (choice == 4)
        {
            builder.Append(random.Next(2) == 0 ? "min(" : "max(");
            Generate(random, builder, depth - 1);
            builder.Append(", ");
            Generate(random, builder, depth - 1);
            builder.Append(")");
        }
        else if (choice == 5)
        {
            builder.Append("if(");
            Generate(random, builder, depth - 1);
            builder.Append(random.Next(2) == 0 ? " < " : " == ");
            Generate(random, builder, depth - 1);
            builder.Append(", ");
            Generate(random, builder, depth - 1);
            builder.Append(", ");
            Generate(random, builder, depth - 1);
            builder.Append(")");
        }
        else
        {
            builder.Append("(");
            Generate(random, builder, depth - 1);
            builder.Append(Operators[random.Next(Operators.Length)]);
            Generate(random, builder, depth - 1);
            builder.Append(")");
        }
    }

    private static string ScriptSource(Random random)
    {
        StringBuilder builder = new StringBuilder();
        builder.Append("x = square(x)");
        for (int i = 0; i < ChainDepth; i++)
        {
            builder.Append(" + ").Append(Names[i % Names.Length]);
        }
        builder.Append(";\n");
        for (int i = 1; i < StatementCount; i++)
        {
            builder.Append(Names[random.Next(Names.Length)]).Append(" = ");
            Generate(random, builder, MaxDepth);
            builder.Append(";\n");
        }
        return builder.ToString();
    }

    private static List<Statement> Parse(string source)
    {
        return new Parser(Tokenize(source)).Script();
    }

    public static int Main(string[] args)
    {
        if (args.Length != 1 ||
            !int.TryParse(args[0], NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out var rounds) ||
            rounds < 0)
        {
            return 1;
        }
        Console.Write(Workload(rounds));
        return 0;
    }

    public static string Workload(int rounds)
    {
        Random random = new Random(7177);
        string source = ScriptSource(random);
        List<Statement> statements = Parse(source);
        Environment env = new Environment();
        long checksum = 0;
        for (int round = 0; round < rounds; round++)
        {
            if (round % ReparseInterval == ReparseInterval - 1)
            {
                statements = Parse(source);
            }
            foreach (string name in Names)
            {
                env.Write(name, random.Next((int)Modulus));
            }
            foreach (Statement statement in statements)
            {
                long value = statement.Expression.Evaluate(env);
                env.Write(statement.Target, value);
                checksum = (checksum * 31 + value) & Mask;
            }
        }
        StringBuilder rendered = new StringBuilder();
        int nodes = 0;
        foreach (Statement statement in statements)
        {
            rendered.Append(statement.Target).Append(" = ");
            statement.Expression.Render(rendered);
            rendered.Append(";\n");
            nodes += statement.Expression.Size();
        }
        long textChecksum = 0;
        foreach (byte b in Encoding.UTF8.GetBytes(rendered.ToString()))
        {
            textChecksum = (textChecksum * 31 + b) & Mask;
        }
        return FormattableString.Invariant($"expression-interpreter {rounds} {statements.Count} {nodes} {Encoding.UTF8.GetByteCount(source)} {checksum} {textChecksum}\n");
    }
}
