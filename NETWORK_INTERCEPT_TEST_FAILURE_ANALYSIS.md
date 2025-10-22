# Network Intercept Test Failure Analysis

## Problema

O teste `NetworkInterceptBiDiTest::InterceptSimpleRequest` falha com a mensagem:
```
C++ exception with description "Intercept não ocorreu" thrown in the test body.
```

## Raiz Causa

### Sequência de Execução Problemática

1. `NetworkInterceptHandler::create()` retorna um `Task<...>` lazy
2. A corrotina interna é spawned com `asio::detached` (linha 23 em `bidi_network_intercept_handler.cpp`)
3. O handler é retornado enquanto a corrotina de registro de event handlers ainda está rodando
4. O teste tenta navegar imediatamente após criação
5. O evento `network.beforeRequestSent` chega do browser **antes** do handler estar registrado
6. O evento é perdido → callback nunca é invocado → teste falha por timeout

### Código Problemático

```cpp
// Em bidi_network_intercept_handler.cpp, linha ~23
auto NetworkInterceptHandler::create(...) -> Task<std::shared_ptr<NetworkInterceptHandler>> {
    auto result = Task<std::shared_ptr<NetworkInterceptHandler>>::make(ex, loc);
    
    boost::asio::co_spawn(
        ex,
        [client, config = std::move(config), result, loc]() mutable -> ... {
            try {
                auto intercept_id = co_await client->add_intercept(...);
                auto handler = NetworkInterceptHandler(...);
                auto handler_ptr = std::make_shared<NetworkInterceptHandler>(std::move(handler));
                
                // ← PROBLEMA: Registrando handlers AQUI
                for (const auto &phase : handler_ptr->config_.phases) {
                    (void)client->set_event_handler(...);  // ← Assíncrono, não aguardado!
                }
                
                result.fulfill(handler_ptr);  // ← Retorna aqui ANTES dos handlers estarem prontos
            } catch (...) {
                result.fail(std::current_exception());
            }
        },
        boost::asio::detached);  // ← DETACHED = Task é retornada antes do registro completar
    
    return result;
}
```

## Comparação com UserPromptHandler (Implementação Correta)

O `UserPromptHandler` usa corretamente `.and_then()` e `.finally()` para aguardar:

```cpp
auto UserPromptHandler::create(...) -> Task<std::shared_ptr<UserPromptHandler>> {
    // 1. Subscribe
    // 2. .finally() aguarda subscription
    // 3. .and_then() cria handler e registra first event handler
    // 4. Aguarda resultado com .finally()
    // 5. Registra second event handler
    // 6. Aguarda resultado com .finally()
    // 7. Retorna handler (TUDO PRONTO)
    
    client->subscribe(events).finally([client, config, result](...) {
        handler_ptr->set_event_handler(...).and_then([...](subs) {
            handler_ptr->opened_subscription_ = std::move(subs);
            return client->set_event_handler_subscription(...);
        }).finally([handler_ptr, result](...) {
            handler_ptr->closed_subscription_ = std::move(*subs);
            result.fulfill(handler_ptr);  // ← Agora SIM, tudo pronto!
        });
    });
}
```

## Pontos de Injeção para Correção

### Opção 1: Usar `.and_then()` e `.finally()` (Recomendado)
- **Localização**: Refatorar `NetworkInterceptHandler::create()`
- **Vantagem**: Consistente com `UserPromptHandler`
- **Desvantagem**: Código mais complexo com callbacks encadeados

### Opção 2: Esperar por `set_event_handler_subscription()` antes de retornar
- **Localização**: `NetworkInterceptHandler::create()` - linha ~50
- **Vantagem**: Similar a UserPromptHandler
- **Implementação**: Guardar `std::shared_ptr<BiDiSession::Subscription>` para cada fase

### Opção 3: Adicionar helper de espera de eventos no teste
- **Localização**: Teste ou novo método em `NetworkInterceptHandler`
- **Menos ideal**: Deixa problema na implementação

### Opção 4: Criar corrotina de bombeamento de eventos
- **Localização**: `NetworkInterceptHandler` ou teste
- **Implementação**: `co_await handler->pump_events(timeout);`
- **Menos ideal**: Adiciona complexidade ao handler

## Solução Recomendada: Opção 2

Modificar `NetworkInterceptHandler::create()` para:

1. Chamar `client->add_intercept()` e aguardar
2. Criar handler 
3. Para cada fase, chamar `set_event_handler_subscription()` e **aguardar resultado**
4. Guardar retorno em `handler->phase_subscriptions[phase]`
5. Retornar handler **DEPOIS** de todos os handlers estarem registrados

Isto garante que quando `co_await NetworkInterceptHandler::create(...)` retorna, 
todos os event handlers já estão prontos para receber eventos.

## Pontos de Injeção de Código

### Arquivo: `src/bidi_network_intercept_handler.cpp`
- **Linha ~23**: Refatorar `create()` para usar `.and_then()` chain
- **Adicionar**: Guardar `Subscription` para cleanup no destrutor

### Arquivo: `include/bidi/network_intercept_handler.hpp`
- **Adicionar**: Membro `std::vector<std::shared_ptr<BiDiSession::Subscription>>` para guardar subscriptions
- **Modificar**: Adicionar field de guarda para subscriptions

## Impacto

- ✅ Teste passará
- ✅ Event handlers garantidamente prontos antes de uso
- ✅ Padrão consistente com `UserPromptHandler`
- ⚠️ Refatoração moderada do código de criação
