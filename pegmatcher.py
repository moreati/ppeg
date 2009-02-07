from _ppeg import Pattern as P

EndOfFile = -P(1)
EndOfLine = P("\r\n") | P("\r") | P("\n")
Space = P(" ") | P("\t") | P.Var("EndOfLine")
Comment = P("#") + (-P.Var("EndOfLine") + P(1)) ** 0 + P.Var("EndOfLine")
Spacing = (P.Var("Space") | P.Var("Comment")) ** 0

LEFTARROW = P("<-") + P.Var("Spacing")
SLASH = P("/") + P.Var("Spacing")
AND = P("&") + P.Var("Spacing")
NOT = P("!") + P.Var("Spacing")
QUESTION = P("?") + P.Var("Spacing")
STAR = P("*") + P.Var("Spacing")
PLUS = P("+") + P.Var("Spacing")
OPEN = P("(") + P.Var("Spacing")
CLOSE = P(")") + P.Var("Spacing")
DOT = P(".") + P.Var("Spacing")

Char = ( P("\\") + P.Set("nrt'\"*[]\\") |
       P("\\") + P.Range("02") + P.Range("07") + P.Range("07") |
       P("\\") + P.Range("07") + P.Range("07") ** -1 |
       -P("\\") + P(1) )

Range = P.Var("Char") + P("-") + P.Var("Char") | P.Var("Char") + P.Var("Spacing")
Class = P("[") + ( -P("]") + P.Var("Range") ) ** 0 + P("]") + P.Var("Spacing")
Literal = (P("'") + ( -P("'") + P.Var("Char") ) ** 0 + P("'") + P.Var("Spacing") |
          P('"') + ( -P('"') + P.Var("Char") ) ** 0 + P('"') + P.Var("Spacing")
          )

Identifier = P.Var("IdentStart") + P.Var("IdentCont") ** 0 + P.Var("Spacing")
IdentStart = P.Range("azAZ") | P("_")
IdentCont = P.Var("IdentStart") | P.Range("09")

Primary = (P.Var("Identifier") + -P.Var("LEFTARROW") |
          P.Var("OPEN") + P.Var("Expression") + P.Var("CLOSE") |
          P.Var("Literal") |
          P.Var("Class") |
          P.Var("DOT")) + P.Var("Spacing")

Suffix = (P.Var("Primary") + 
          (P.Var("QUESTION") | P.Var("STAR") | P.Var("PLUS")) ** -1 +
          P.Var("Spacing"))
Prefix = (P.Var("AND") | P.Var("NOT")) ** -1 + P.Var("Suffix")
Sequence = P.Var("Prefix") ** 0
Expression = P.Var("Sequence") + (P.Var("SLASH") + P.Var("Sequence")) ** 0

Definition = P.Var("Identifier") + P.Var("LEFTARROW") + P.Var("Expression")

Grammar = P.Var("Spacing") + P.Var("Definition") ** 1 + P.Var("EndOfFile")

PEG = P.Grammar(
        Grammar,
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
