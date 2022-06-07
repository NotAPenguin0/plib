#include <pscript/context.hpp>

#include <peglib.h>

#include <iostream>
#include <fstream>
#include <string>

// TODO: proper error reporting

namespace ps {

// Not too proud of this one, but moving to an external file is also not optimal
static const char* grammar = R"(
# --------------------------------
# Explanation of basic PEG syntax:
# --------------------------------
#
# Rules are defined as
# rulename <- match
# To define what matches a rule, items can be sequenced together by simply putting them next to each other.
# myrule <- 'A' 'B'
# will match 'AB' to myrule.
# rules can contain other rules:
# myrule <- rulea ruleb
# rulea <- 'A'
# ruleb <- 'B'
#
# There are several operators that can be used to make more complicated rules.
# The '/' operator is a prioritized choice:
# rule <- rule_a / rule_b
# will match rule_a or rule_b, but prefer rule_a in case both are possible.
#
# Normal regular expression operators such as * (match any amount), + (match at least one), and ? (match zero or one) are also allowed.
# for more info on this exact version of PEG, see https://github.com/yhirose/cpp-peglib and https://bford.info/pub/lang/peg.pdf


# ================= base content =================

script <- content

# content makes up the main part of the AST. It stores the entire file, one 'logical' line at a time.
# There are four types of 'logical' lines.
# - comments - Start with //, simple comment like in any language. Spans the whole line.
# - elements - These are basic statements that can be found inside (or outside) functions.
# - namespace declarations
# - functions - Starts a function declaration.
# - structs - Starts a struct declaration
content <- (comment / element / namespace_decl / function / struct)* { no_ast_opt }

# ================= basic syntactical symbols =================

space <- ' '*
operator <- < '+=' / '-=' / '*=' / '/=' / '<=' / '>=' / '==' / '!=' / '*' / '/' / '+' / '-' / '<' / '>' / '=' >
unary_operator <- '-' / '++' / '--' / '!'
assign <- '='
colon <- ':'
quote <- '"'
parens_open <- < '(' >
parens_close <- < ')' >
brace_open <- '{'
brace_close <- '}'
list_open <- '['
list_close <- ']'
arrow <- '->'
dot <- '.'
star <- '*'
comma <- ','
semicolon <- ';'
# todo: find better 'any' than this.
any <- [a-zA-Z0-9.,:;_+*/=?!(){}<> ]*
# our language ignores whitespace
%whitespace <- [ \n\t\r]*

# ================= identifiers and literals =================

# identifiers can only start with a lower or uppercase letter, and contain letters, numbers and underscores otherwise.
identifier <- ([a-zA-Z] [a-zA-Z_0-9]*)
# a literal is currently either a string or a number.
literal <- boolean / string / number
number <- float / integer
integer <- < [0-9]+ >
float <- < [0-9]+.[0-9] >
string <- < quote any quote >
boolean <- < 'true' / 'false' >

# ================= typenames =================

typename <- builtin_type / namespace_list? identifier
# typenames can be prefixed by namespace qualifiers
namespace_list <- (namespace '.')+ { no_ast_opt }
namespace <- identifier
# match builtin types separately for easier interpreting
builtin_type <- 'int' / 'float' / 'str' / 'list' / 'any'

# ================= namespaces =================

namespace_decl <- 'namespace ' identifier space brace_open content brace_close

# ================= functions =================

# for functions we need to be able to create parameter lists.
parameter_list <- parameter (comma parameter)* { no_ast_opt }
parameter <- identifier colon typename

# a function can either be an externally declared function, or a function definition.
function <- function_ext / function_def

# extern fn my_external_function(param1: typename, param2: typename) -> return_type;
function_ext <- 'extern fn ' identifier parens_open parameter_list? parens_close arrow typename semicolon

# fn my_function(param1: typename, param2: typename) -> return_type { function_body }
function_def <- 'fn ' identifier parens_open parameter_list? parens_close arrow typename space compound

builtin_function <- '__print' / '__readln'

# ================= structs =================

# struct my_struct {
#   a: float;
#   b: int = 0;
# };
struct <- 'struct ' identifier space brace_open struct_items brace_close semicolon
struct_items <- ((struct_item semicolon) / comment)*
struct_item <- identifier colon typename struct_initializer?
struct_initializer <- assign expression

# basic statement, control structure such as if/while, or a for loop.
element <- comment / statement / if / while / for

# ================= statements =================

# a statement can be
# - an import statement
# - a return statement
# - a variable declaration
# - an expression (usually a call expression)

statement <- statement_base semicolon
statement_base <- import / return / declaration / expression

# ================= import statements =================

# import folder.subfolder.xyz.module;

import <- 'import ' (module_folder dot)* module_name
module_folder <- identifier
module_name <- identifier

# ================= return statements =================

return <- 'return ' expression? { no_ast_opt }

# ================= variable declarations =================

declaration <- 'let ' identifier space assign space expression

# ================= compound statements

compound <- element / (brace_open element* brace_close) { no_ast_opt }

# ================= expressions =================

# There are six kinds of expressions that each need to be parsed differently.
# - a constructor expression in the form MyType{arguments...}
# - a list expression in the form [list_elements...]
# - an 'operator' epxression in the form 'expression operator expression' (ex. a == 8)
# - a call expression in the form my_function(arguments...)
# - an indexing expression x[y]
# - a member access expression x->y
expression <- constructor_expression / op_expression / index_expression / list_expression / call_expression / access_expression

# ----- constructor epxression -----
constructor_expression <- identifier space '{' argument_list? '}'

# ----- list expression -----
list_expression <- list_open argument_list? list_close

# ----- operator expression -----
op_expression <- atom (operator atom)* {
    precedence
    L = += -= *= /=
    L == != <= >= < >
    L - +
    L / *
}
# this is to fully support recursive expressions.
atom <- unary_operator? (access_expression / parens_open expression parens_close / index_expression / list_expression / call_expression / parens_open operand parens_close / operand)
operand <- < literal / identifier >

# ----- call expression -----
call_expression <- namespace_list? (identifier / builtin_function) space parens_open argument_list? parens_close
argument_list <- argument ( comma argument )* { no_ast_opt }
argument <- expression

# ----- indexing expression -----
index_expression <- identifier list_open expression list_close

# ----- member access expression -----
access_expression <- (identifier arrow)+ identifier space


# ================= control sequences =================

# ----- if/else statement -----
if <- 'if' parens_open expression parens_close compound else?
else <- 'else' compound { no_ast_opt }

# ----- while statement -----
while <- 'while' parens_open expression parens_close compound

# ----- for statement -----

for <- 'for' parens_open for_content parens_close compound
# note that there are two types of for loops: for-each loops and regular 'manual' for loops.
for_content <- for_manual / for_each
for_manual <- declaration semicolon expression semicolon expression
for_each <- 'let ' identifier space colon space expression

# ================= comment =================

comment <- '//' any '\n'
)";

context::context(std::size_t mem_size) : mem(mem_size) {
    ast_parser = std::make_unique<peg::parser>(grammar);
    ast_parser->enable_ast();
    ast_parser->enable_packrat_parsing();
}

ps::memory_pool& context::memory() noexcept {
    return mem;
}

ps::memory_pool const& context::memory() const noexcept {
    return mem;
}

void context::dump_memory() const noexcept {
    auto print = [this](ps::pointer ptr) {
        auto const v = static_cast<uint8_t>(mem[ptr]);
        printf("%02X", v);
    };

    for (auto it = mem.begin(); it != mem.end(); it += 32) {
        // Print lines of 32 bytes, grouped in blocks of 8 (as this is the smallest possible block size)
        for (int i = 0; i < 32; i += 8) {
            print(it + i);
            print(it + i + 1);
            print(it + i + 2);
            print(it + i + 3);
            printf(" ");
        }
        printf("\n");
    }
}

peg::parser const& context::parser() const noexcept {
    return *ast_parser;
}

ps::variable& context::create_variable(std::string const& name, ps::value&& initializer, block_scope* scope) {
    auto& variables = scope ? scope->local_variables : global_variables;
    if (auto old = variables.find(name); old != variables.end()) {
        // Variable already exists, so shadow it with a new type by assigning a new value to it.
        // We first need to free the old memory
        memory().free(old->second.value().pointer());
        old->second.value() = std::move(initializer);
        return old->second;
    } else {
        auto it = variables.insert({name, ps::variable(name, std::move(initializer))});
        // make sure name string view points to the entry in the map, so it is guaranteed to live as long as the variable lives.
        it.first->second.set_name(it.first->first);
        return it.first->second;
    }
}


ps::variable& context::get_variable(std::string const& name, block_scope* scope) {
    ps::variable* var = find_variable(name, scope);
    if (!var) throw std::runtime_error("variable not declared in current scope: " + name);
    else return *var;
}

[[nodiscard]] ps::variable* context::find_variable(std::string const& name, block_scope* scope) {
    auto& variables = scope ? scope->local_variables : global_variables;
    auto it = variables.find(name);
    if (it == variables.end()) {
        // if we were looking in local scope, also try parent scope
        if (scope) {
            return find_variable(name, scope->parent);
        } else {
            return nullptr;
        }
    }
    else return &it->second;
}

ps::value& context::get_variable_value(std::string const& name, block_scope* scope) {
    return get_variable(name, scope).value();
}


void context::execute(ps::script const& script, ps::execution_context exec) {
    std::shared_ptr<peg::Ast> const& ast = script.ast();
    exec_ctx = exec;
    execute(ast.get(), nullptr); // start execution in global scope
}

ps::value context::execute(peg::Ast const* node, block_scope* scope, std::string const& namespace_prefix) {
    // TODO: Rework return value system to put return value in call stack instead!
    if (node_is_type(node, "declaration")) {
        evaluate_declaration(node, scope);
    }

    if (node_is_type(node, "function")) {
        evaluate_function_definition(node, namespace_prefix);
    }

    if (node_is_type(node, "struct")) {
        evaluate_struct_definition(node, namespace_prefix);
    }

    if (node_is_type(node, "call_expression")) {
        return evaluate_function_call(node, scope);
    }

    // sometimes expressions can occur "in the wild", for example 'n = 5' or 'n += 6'
    if (node_is_type(node, "op_expression")) {
        evaluate_expression(node, scope);
    }

    auto has_returned = [this]() {
        return !call_stack.empty() && call_stack.top().return_val != std::nullopt;
    };

    if (node_is_type(node, "import")) {
        evaluate_import(node);
    }

    if (node_is_type(node, "statement") || node_is_type(node, "compound") || node_is_type(node, "script") || node_is_type(node, "content")) {
        for (auto const& child : node->nodes) {
            execute(child.get(), scope, namespace_prefix);

            if (has_returned()) return *call_stack.top().return_val;
        }
    }

    if (node_is_type(node, "return")) {
        call_stack.top().return_val = ps::value::null();
        // first child node of a return statement is the return expression.
        if (!node->nodes.empty()) {
            call_stack.top().return_val = evaluate_expression(node->nodes[0].get(), scope);
        }
    }

    if (node_is_type(node, "if")) {
        peg::Ast const* condition_node = find_child_with_type(node, "expression");
        ps::value condition = evaluate_expression(condition_node, scope);
        // If the condition evaluates to true, we can execute the compound block with a new scope
        block_scope local_scope {};
        local_scope.parent = scope;
        if (static_cast<bool>(condition)) {
            peg::Ast const* compound = find_child_with_type(node, "compound");
            execute(compound, &local_scope);
        } else {
            // if an else block is present, execute it
            peg::Ast const* else_block = find_child_with_type(node, "else");
            if (else_block) {
                execute(find_child_with_type(else_block, "compound"), &local_scope);
            }
        }
    }

    if (node_is_type(node, "while")) {
        peg::Ast const* condition_node = find_child_with_type(node, "expression");
        peg::Ast const* compound = find_child_with_type(node, "compound");
        while(static_cast<bool>(evaluate_expression(condition_node, scope))) {
            block_scope local_scope {};
            local_scope.parent = scope;
            execute(compound, &local_scope);
        }
    }

    if (has_returned()) return *call_stack.top().return_val;
    else return ps::value::null();
}

peg::Ast const* context::find_child_with_type(peg::Ast const* node, std::string_view type) const noexcept {
    for (auto const& child : node->nodes) {
        if (child->original_name == type || child->name == type) return child.get();
    }
    return nullptr;
}

bool context::node_is_type(peg::Ast const* node, std::string_view type) const noexcept {
    return node->name == type || node->original_name == type;
}

void context::evaluate_declaration(peg::Ast const* node, block_scope* scope) {
    peg::Ast const* identifier = find_child_with_type(node, "identifier");
    peg::Ast const* initializer = find_child_with_type(node, "expression");

    if (!identifier) throw std::runtime_error("[decl] expected identifier");
    if (!initializer) throw std::runtime_error("[decl] expected initializer");

    ps::value init_val = evaluate_expression(initializer, scope);

    ps::variable& var = create_variable(identifier->token_to_string(), std::move(init_val), scope);
}

void context::evaluate_function_definition(peg::Ast const* node, std::string const& namespace_prefix) {
    peg::Ast const* identifier = find_child_with_type(node, "identifier");
    peg::Ast const* params = find_child_with_type(node, "parameter_list");

    peg::Ast const* content = find_child_with_type(node, "compound");
    function func {};
    func.node = content;
    if (params) {
        for (auto const& child : params->nodes) {
            if (!node_is_type(child.get(), "parameter")) continue;
            peg::Ast const* param_name = find_child_with_type(child.get(), "identifier");
            // TODO: extract type information
            func.params.push_back(function::parameter{ .name = param_name->token_to_string() });
        }
    }
    // TODO: return type information?
    std::string name = namespace_prefix + identifier->token_to_string();
    auto it = functions.insert({name, std::move(func)});
    // set key reference
    it.first->second.name = it.first->first;
}

void context::evaluate_struct_definition(peg::Ast const* node, std::string const& namespace_prefix) {
    peg::Ast const* identifier = find_child_with_type(node, "identifier");
    peg::Ast const* members = find_child_with_type(node, "struct_items");
    struct_description info {};

    if (members) {
        for (auto const& field : members->nodes) {
            if (!node_is_type(field.get(), "struct_item")) continue;

            peg::Ast const* name = find_child_with_type(field.get(), "identifier");
            peg::Ast const* initializer = find_child_with_type(field.get(), "struct_initializer");
            peg::Ast const* init_expression = find_child_with_type(initializer, "expression");
            struct_description::member field_info {
                name->token_to_string(),
                evaluate_expression(init_expression, nullptr)
            };
            info.members.push_back(std::move(field_info));
        }
    }

    std::string name = namespace_prefix + identifier->token_to_string();
    auto it = structs.insert({ name, std::move(info) });
    it.first->second.name = it.first->first;
}

static std::string read_script(std::string const& filename) {
    std::ifstream file {filename};
    return std::string { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
}

void context::evaluate_import(peg::Ast const* node) {
    std::vector<std::string> folders = {};
    for (auto const& child : node->nodes) {
        if (node_is_type(child.get(), "module_folder")) {
            folders.push_back(child->token_to_string());
        }
    }

    peg::Ast const* module_name = find_child_with_type(node, "module_name");

    // resolve module folders + name into a module file
    std::string filepath = "pscript-modules/";
    for (auto const& folder : folders) {
        filepath += folder + '/';
    }
    filepath += module_name->token_to_string() + ".ps";

    // import it
    imported_scripts.emplace_back(read_script(filepath), *this);
    peg::Ast const* ast = imported_scripts.back().ast().get();

    // build namespace string
    std::string namespace_prefix;
    for (auto const& folder : folders) {
        namespace_prefix += folder + '.';
    }
    namespace_prefix += module_name->token_to_string() + '.';
    // run imported scripts in a local scope to make sure variables dont collide.
    block_scope local_scope {};
    execute(ast, &local_scope, namespace_prefix);
}

ps::value context::evaluate_operand(peg::Ast const* node, block_scope* scope) {
    assert(node_is_type(node, "operand"));

    std::string str_repr = node->token_to_string();
    // integer or floating point literal
    if (std::isdigit(str_repr[0])) {
        if (str_repr.find('.') != std::string::npos) {
            return ps::value::from(memory(), node->token_to_number<ps::real::value_type>());
        } else {
            return ps::value::from(memory(), node->token_to_number<ps::integer::value_type>());
        }
    }

    // string literal
    if (str_repr[0] == '\"') {
        if (str_repr.size() > 2) {
            return ps::value::from(memory(), ps::str::value_type { str_repr.substr(1, str_repr.length() - 2) });
        } else {
            // empty string
            return ps::value::from(memory(), ps::str::value_type {});
        }
    }

    // identifier
    return get_variable_value(str_repr, scope);
}

ps::value context::evaluate_operator(peg::Ast const* lhs, peg::Ast const* op, peg::Ast const* rhs, block_scope* scope) {
    ps::value left = evaluate_expression(lhs, scope);
    ps::value right = evaluate_expression(rhs, scope);

    std::string op_str = op->token_to_string();
    if (op_str == "+") return left + right;
    if (op_str == "*") return left * right;
    if (op_str == "-") return left - right;
    if (op_str == "/") return left / right;
    if (op_str == "==") return left == right;
    if (op_str == "!=") return left != right;
    if (op_str == "<") return left < right;
    if (op_str == ">") return left > right;
    if (op_str == ">=") return left >= right;
    if (op_str == "<=") return left <= right;

    // all other operators are 'mutable' operators, meaning they modify the left-hand side in some way or another.
    // in this case we would like to find out if the left side is an assignable identifier.
    // if so, we do the assignment

    ps::value* value = nullptr;
    // special case for list index expressions
    if (node_is_type(lhs, "index_expression")) {
        value = &index_list(lhs, scope);
    } else if (node_is_type(lhs, "access_expression")) {
        value = &access_member(lhs, scope);
    } else {
        std::string tok = lhs->token_to_string();
        ps::variable& var = get_variable(tok, scope);
        value = &var.value();
    }
    if (op_str == "=") return *value = right;
    if (op_str == "+=") return *value += right;
    if (op_str == "-=") return *value -= right;
    if (op_str == "*=") return *value *= right;
    if (op_str == "/=") return *value /= right;

    else throw std::runtime_error("[operator] operator " + op_str + " not implemented");
}

std::vector<ps::value> context::evaluate_argument_list(peg::Ast const* call_node, block_scope* scope) {
    peg::Ast const* list = find_child_with_type(call_node, "argument_list");
    if (!list) return {};
    std::vector<ps::value> values {};
    values.reserve(list->nodes.size());
    for (auto const& child :  list->nodes) {
        if (node_is_type(child.get(), "argument")) {
            values.push_back(evaluate_expression(child.get(), scope));
        }
    }
    return values;
}

std::string context::parse_namespace(peg::Ast const* node) {
    std::string result;
    for (auto const& child : node->nodes) {
        if (node_is_type(child.get(), "namespace")) {
            result += ('.' + child->token_to_string());
        }
    }
    // remove first dot
    result.erase(0, 1);
    return result;
}

void context::prepare_function_scope(peg::Ast const* call_node, block_scope* call_scope, function* func, block_scope* func_scope) {
    using namespace std::literals::string_literals;
    func_scope->parent = nullptr; // parent is global scope for function calls (as you can't access variables from previous scope, unlike in if statements).

    auto arguments = evaluate_argument_list(call_node, call_scope);
    // no work
    if (arguments.empty()) return;

    if (arguments.size() != func->params.size()) throw std::runtime_error(
            "[func_call] "s + func->name.data() + ": expected " + std::to_string(func->params.size()) + " arguments, got " + std::to_string(arguments.size()));

    // create variables with function arguments in call scope
    for (size_t i = 0; i < arguments.size(); ++i) {
        ps::variable& _ = create_variable(func->params[i].name, std::move(arguments[i]), func_scope);
    }
}

ps::value context::evaluate_function_call(peg::Ast const* node, block_scope* scope) {
    peg::Ast const* builtin_identifier = find_child_with_type(node, "builtin_function");
    if (builtin_identifier) return evaluate_builtin_function(builtin_identifier->token_to_string(), node, scope);

    peg::Ast const* namespace_identifier = find_child_with_type(node, "namespace_list");
    peg::Ast const* func_identifier_node = find_child_with_type(node, "identifier");

    // namespaced functions are simply stored by concatenating their names together to make the full name, but we won't implement that yet
    // TODO: implement namespaces
    std::string func_name = func_identifier_node->token_to_string();

    std::string namespace_name;
    if (namespace_identifier) {
        namespace_name = parse_namespace(namespace_identifier);
    }

    if (!namespace_name.empty()) {
        // check if namespace name is a variable, if so we are calling a builtin member function (for list objects for example).
        ps::variable* var = find_variable(namespace_name, scope);

        if (var) {
            ps::type const type = var->value().get_type();
            if (type == ps::type::list) {
                return evaluate_list_member_function(func_name, *var, node, scope);
            } else if (type == ps::type::str) {
                return evaluate_string_member_function(func_name, *var, node, scope);
            }
        } else { // this is a regular function call, still make sure to set lookup name properly
            func_name = namespace_name + '.' + func_name;
        }
    }

    auto it = functions.find(func_name);
    if (it == functions.end()) {
        throw std::runtime_error("[func call] function " + func_name + " not found.\n");
    }

    // create function scope for this call
    block_scope local_scope {};

    prepare_function_scope(node, scope, &it->second, &local_scope);
    call_stack.push(function_call {.func = &it->second, .scope = &local_scope });
    ps::value val = execute(it->second.node, &local_scope);
    call_stack.pop();
    return val;
}

ps::value context::evaluate_list_member_function(std::string_view name, ps::variable& object, peg::Ast const* node, block_scope* scope) {
    auto arguments = evaluate_argument_list(node, scope);

    ps::value& val = object.value();
    if (name == "append") {
        if (arguments.size() != 1) throw std::runtime_error("[list::append] - expected 1 argument");
        static_cast<ps::list&>(val)->append(arguments.front());
    }

    if (name == "size") {
        return ps::value::from(memory(), (int)static_cast<ps::list&>(val)->size());
    }

    return ps::value::null();
}

ps::value context::evaluate_string_member_function(std::string_view name, ps::variable& object, peg::Ast const* node, block_scope* scope) {
    auto arguments = evaluate_argument_list(node, scope);

    ps::value& val = object.value();
    auto const& str = static_cast<ps::str const&>(val);
    if (name == "format") {
        return ps::value::from(memory(), str->format(arguments));
    }

    if (name == "parse_int") {
        return ps::value::from(memory(), str->parse_int());
    }

    if (name == "parse_float") {
        return ps::value::from(memory(), str->parse_float());
    }

    return ps::value::null();
}

ps::value context::evaluate_builtin_function(std::string_view name, peg::Ast const* node, block_scope* scope) {
    auto arguments = evaluate_argument_list(node, scope);
    // builtin function: print
    // TODO: print improvements (format string)
    if (name == "__print") {
        if (arguments.empty()) throw std::runtime_error("[__print()] invalid argument count.");
        ps::value const& to_print = arguments[0];
        *exec_ctx.out << to_print << std::endl;
        // success
        return ps::value::from(memory(), 0);
    } else if (name == "__readln") {
        std::string input {};
        std::getline(*exec_ctx.in, input);
        return ps::value::from(memory(), string_type { input });
    }

    return ps::value::null();
}

ps::value context::evaluate_list(peg::Ast const* node, block_scope* scope) {
   auto arguments = evaluate_argument_list(node, scope);
   return ps::value::from(memory(), ps::list_type{ arguments });
}

ps::value context::evaluate_constructor_expression(peg::Ast const* node, block_scope* scope) {
    auto arguments = evaluate_argument_list(node, scope);
    // TODO: add support for builtin types here!
    peg::Ast const* type = find_child_with_type(node, "identifier");
    std::string struct_name = type->token_to_string();
    auto it = structs.find(struct_name);
    if (it == structs.end()) throw std::runtime_error("Struct '" + struct_name + "' not defined in current scope.");
    auto const& struct_def = it->second;
    std::unordered_map<std::string, ps::value> initializers;
    for (int i = 0; i < arguments.size(); ++i) {
        initializers.insert({ struct_def.members[i].name, std::move(arguments[i]) });
    }
    // add default initializers
    for (int j = arguments.size(); j < struct_def.members.size(); ++j) {
        initializers.insert({struct_def.members[j].name, struct_def.members[j].default_value});
    }

    return ps::value::from(memory(), ps::struct_type { initializers });
}

ps::value& context::index_list(peg::Ast const* node, block_scope* scope) {
    peg::Ast const* identifier = find_child_with_type(node, "identifier");
    peg::Ast const* index_expr = find_child_with_type(node, "expression");

    ps::value index_expr_val = evaluate_expression(index_expr, scope);
    auto& index = static_cast<ps::integer&>(index_expr_val);
    auto& list_val = get_variable_value(identifier->token_to_string(), scope);
    auto& list = static_cast<ps::list&>(list_val);

    ps::value& value = list->get(index.value());
    return value;
}

ps::value& context::access_member(peg::Ast const* node, block_scope* scope) {
    peg::Ast const* identifier = find_child_with_type(node, "identifier");
    ps::variable& var = get_variable(identifier->token_to_string(), scope);
    ps::value* cur_val = &var.value();
    for (auto const& child : node->nodes) {
        if (child.get() == identifier) continue; // skip initial node
        if (!node_is_type(child.get(), "identifier")) continue; // only process identifiers
        auto& as_struct = static_cast<ps::structure&>(*cur_val);
        cur_val = &as_struct->access(child->token_to_string());
    }
    return *cur_val;
}

ps::value context::evaluate_expression(peg::Ast const* node, block_scope* scope) {
    // base case, an operand is a simple value.
    if (node_is_type(node, "operand")) {
        return evaluate_operand(node, scope);
    }

    // the atom[operand] case was already handled above.
    if (node_is_type(node, "atom")) {
        // skip over children until we find a child node with a type that we need.
        // this is because there are parens_open and parens_close nodes in here
        for (auto const& child : node->nodes) {
            if (node_is_type(child.get(), "expression")) {
                return evaluate_expression(child.get(), scope);
            }

            if (node_is_type(child.get(), "unary_operator")) {
                peg::Ast const* operand = find_child_with_type(node, "operand");
                std::string op = child->token_to_string();
                if (op == "-") {
                    return -evaluate_expression(operand, scope);
                }
            }
        }
    }

    if (node_is_type(node, "call_expression")) {
        return evaluate_function_call(node, scope);
    }

    if (node_is_type(node, "op_expression")) {
        peg::Ast const* lhs_node = node->nodes[0].get();
        peg::Ast const* operator_node = node->nodes[1].get();
        peg::Ast const* rhs_node = node->nodes[2].get();

        return evaluate_operator(lhs_node, operator_node, rhs_node, scope);
    }

    if (node_is_type(node, "constructor_expression")) {
        return evaluate_constructor_expression(node, scope);
    }

    if (node_is_type(node, "list_expression")) {
        return evaluate_list(node, scope);
    }

    if (node_is_type(node, "index_expression")) {
        return index_list(node, scope);
    }

    if (node_is_type(node, "access_expression")) {
        return access_member(node, scope);
    }

    return ps::value::null();
}

} // namespace ps