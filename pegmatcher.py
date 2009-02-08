from _ppeg import Pattern as P

class Var:
    def __getattr__(self, name):
        return P.Var(name)
V = Var()

EndOfFile = -P(1)
EndOfLine = P("\r\n") | P("\r") | P("\n")
Space = P(" ") | P("\t") | V.EndOfLine
Comment = P("#") + (-V.EndOfLine + P(1)) ** 0 + V.EndOfLine
Spacing = (V.Space | V.Comment) ** 0

LEFTARROW = P("<-") + V.Spacing
SLASH = P("/") + V.Spacing
AND = P("&") + V.Spacing
NOT = P("!") + V.Spacing
QUESTION = P("?") + V.Spacing
STAR = P("*") + V.Spacing
PLUS = P("+") + V.Spacing
OPEN = P("(") + V.Spacing
CLOSE = P(")") + V.Spacing
DOT = P(".") + V.Spacing

Char = ( P("\\") + P.Set("nrt'\"*[]\\") |
       P("\\") + P.Range("02") + P.Range("07") + P.Range("07") |
       P("\\") + P.Range("07") + P.Range("07") ** -1 |
       -P("\\") + P(1) )

Range = V.Char + P("-") + V.Char | V.Char + V.Spacing
Class = P("[") + ( -P("]") + V.Range ) ** 0 + P("]") + V.Spacing
Literal = (P("'") + ( -P("'") + V.Char ) ** 0 + P("'") + V.Spacing |
          P('"') + ( -P('"') + V.Char ) ** 0 + P('"') + V.Spacing
          )

Identifier = V.IdentStart + V.IdentCont ** 0 + V.Spacing
IdentStart = P.Range("azAZ") | P("_")
IdentCont = V.IdentStart | P.Range("09")

Primary = (V.Identifier + -V.LEFTARROW |
          V.OPEN + V.Expression + V.CLOSE |
          V.Literal |
          V.Class |
          V.DOT) + V.Spacing

Suffix = (V.Primary + 
          (V.QUESTION | V.STAR | V.PLUS) ** -1 +
          V.Spacing)
Prefix = (V.AND | V.NOT) ** -1 + V.Suffix
Sequence = V.Prefix ** 0
Expression = V.Sequence + (V.SLASH + V.Sequence) ** 0

Definition = V.Identifier + V.LEFTARROW + V.Expression

Grammar = V.Spacing + V.Definition ** 1 + V.EndOfFile

PEG = P.Grammar(
        start = "Grammar",
        Grammar = Grammar,
        Definition = Definition,
        Expression = Expression,
        Sequence = Sequence,
        Prefix = Prefix,
        Suffix = Suffix,
        Primary = Primary,
        Identifier = Identifier,
        IdentStart = IdentStart,
        IdentCont = IdentCont,
        Literal = Literal,
        Class = Class,
        Range = Range,
        Char = Char,
        LEFTARROW = LEFTARROW,
        SLASH = SLASH,
        AND = AND,
        NOT = NOT,
        QUESTION = QUESTION,
        STAR = STAR,
        PLUS = PLUS,
        OPEN = OPEN,
        CLOSE = CLOSE,
        DOT = DOT,
        Spacing = Spacing,
        Comment = Comment,
        Space = Space,
        EndOfLine = EndOfLine,
        EndOfFile = EndOfFile
)

# Spacing rules need work!
PEG_GRAMMAR = r"""
# Hierarchical syntax
Grammar <- Spacing Definition+ EndOfFile
Definition <- Identifier LEFTARROW Expression
Expression <- Sequence (SLASH Sequence)*
Sequence <- Prefix*
Prefix <- (AND / NOT)? Suffix
Suffix <- Primary (QUESTION / STAR / PLUS)?
Primary <- Identifier !LEFTARROW
/ OPEN Expression CLOSE
/ Literal / Class / DOT
# Lexical syntax
Identifier <- IdentStart IdentCont* Spacing
IdentStart <- [a-zA-Z_]
IdentCont <- IdentStart / [0-9]
Literal <- ['] (!['] Char)* ['] Spacing
/ ["] (!["] Char)* ["] Spacing
Class <- '[' (!']' Range)* ']' Spacing
Range <- Char '-' Char / Char
Char <- '\\' [nrt'"\[\]\\]
/ '\\' [0-2][0-7][0-7]
/ '\\' [0-7][0-7]?
/ !'\\' .
LEFTARROW <- '<-' Spacing
SLASH <- '/' Spacing
AND <- '&' Spacing
NOT <- '!' Spacing
QUESTION <- '?' Spacing
STAR <- '*' Spacing
PLUS <- '+' Spacing
OPEN <- '(' Spacing
CLOSE <- ')' Spacing
DOT <- '.' Spacing
Spacing <- (Space / Comment)*
Comment <- '#' (!EndOfLine .)* EndOfLine
Space <- ' ' / '\t' / EndOfLine
EndOfLine <- '\r\n' / '\n' / '\r'
EndOfFile <- !.
"""
