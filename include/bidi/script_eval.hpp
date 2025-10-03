// bidi/script_eval.hpp - Estruturas e política para tratamento de exceptions de
// script.evaluate
#pragma once

#include <boost/json.hpp>
#include <optional>
#include <string>
#include <vector>

// Forward declaration minimal de ParsedResponse para evitar incluir core.hpp
// neste header
namespace bidi::core {
struct ParsedResponse;
}

namespace bidi::script {

// Representa um frame de stack de script (quando fornecido pelo driver BiDi)
struct ScriptStackFrame {
    std::string url;                // pode estar vazio
    std::string function_name;      // pode estar vazio
    std::int64_t line_number{-1};   // -1 quando ausente
    std::int64_t column_number{-1}; // -1 quando ausente
};

// Detalhes completos de uma exception de script, preservando subobjeto bruto
struct ScriptExceptionDetails {
    std::string exception_type; // normalmente "exception"
    std::string text;           // exceptionDetails.text
    std::string value;          // exceptionDetails.exception.value
    std::string name;           // exceptionDetails.exception.className
    std::string error_type;     // exceptionDetails.exception.type
    std::optional<std::int64_t> line_number;    // topo
    std::optional<std::int64_t> column_number;  // topo
    std::vector<ScriptStackFrame> stack_frames; // ordem conforme recebido
    boost::json::object
        raw; // snapshot de exceptionDetails (ou vazio se inexistente)
};

// Política de avaliação de script para tratamento de exceptions
enum class script_eval_policy : std::uint8_t {
    throw_on_script_exception,
    return_outcome
};

// Resultado composto opcional (usado quando política == return_outcome)
struct ScriptEvalOutcome {
    boost::json::value result; // valor normal (se não houve exception)
    std::optional<ScriptExceptionDetails>
        exception;           // presente se script lançou
    boost::json::object raw; // sempre: response.result original
    [[nodiscard]] bool has_exception() const noexcept {
        return exception.has_value();
    }
};

// Exceção especializada contendo detalhes completos
class ScriptEvaluateException : public std::runtime_error {
    ScriptExceptionDetails details_;

  public:
    explicit ScriptEvaluateException(ScriptExceptionDetails details)
        : std::runtime_error(details.text.empty() ? "script evaluate exception"
                                                  : details.text),
          details_(std::move(details)) {}
    [[nodiscard]] const ScriptExceptionDetails &details() const noexcept {
        return details_;
    }
};

// API interna: detecta se result representa uma exception de script
[[nodiscard]] bool
is_script_exception_result(const boost::json::object &result) noexcept;

// API interna: extrai ScriptExceptionDetails de um result cujo type ==
// "exception"
[[nodiscard]] ScriptExceptionDetails
parse_script_exception(const boost::json::object &result) noexcept;

// Estrutura auxiliar interna usada pelo overload evaluate para aplicar
// política.
struct PolicyApplicationResult {
    enum class Action : std::uint8_t { fulfill, throw_exception };
    Action action{Action::fulfill};
    ScriptEvalOutcome outcome;        // válido quando action == fulfill
    ScriptExceptionDetails exception; // válido quando action == throw_exception
};

[[nodiscard]] PolicyApplicationResult
apply_policy(const core::ParsedResponse &response,
             script_eval_policy policy) noexcept;

} // namespace bidi::script
