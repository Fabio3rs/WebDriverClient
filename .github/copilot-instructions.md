Leia os outros .md antes de qualquer coisa, depois que entender o projeto me avise.

Nunca use emoji, ou icones em logs, ou mensagens de commit, ou em nomes de branches, ou em nomes de arquivos, ou em nomes de pastas, ou em nomes de variaveis, ou em nomes de funcoes, ou em nomes de classes, ou em nomes de metodos, ou em nomes de pacotes, ou em nomes de modulos, ou em nomes de bibliotecas, ou em nomes de frameworks, ou em nomes de sistemas operacionais, ou em nomes de linguagens de programacao

Sempre use nomes claros e descritivos, que expliquem exatamente o que o codigo faz, ou o que a mensagem de commit significa, ou o que a branch representa, ou o que o arquivo ou pasta contem, ou o que a variavel, funcao, classe, metodo, pacote, modulo, biblioteca, framework, sistema operacional ou linguagem de programacao faz.

Você deve ler o arquivo map.md para entender o sistema. E sempre que eu aprovar uma modificação, expansão, adicção ou remoção de algo, você deve atualizar o arquivo map.md com as informações mais recentes. Quando terminar de ler todos os arquivos, explique o que você entendeu sobre o projeto e como ele está estruturado.
Leia os outros arquivos .md antes de qualquer coisa, depois que entender o projeto me avise.

Nunca, jamais, em tempo algum, atualize o mapa, ou commit sem minha autorização. Nosso processo de desenvolvimento é baseado em revisão e aprovação. Depois que eu aprovar uma modificação, expansão, adição ou remoção de algo, você deve atualizar o arquivo map.md com as informações mais recentes fazer o commit e push.

ANTES DE CRIAR QUALQUER FUNÇÃO, OU MÉTODO EM C/C++, JS, CSS, OU HTML, VOCÊ DEVE TER CERTEZA QUE A FUNÇÃO, OU MÉTODO JÁ NÃO EXISTE. PARA ISSO, PESQUISE NO CÓDIGO EXISTENTE ANTES DE IMPLEMENTAR NOVAS SOLUÇÕES.

## REGRAS CRÍTICAS DE VERIFICAÇÃO ANTES DE QUALQUER MODIFICAÇÃO

ANTES DE ESCREVER QUALQUER LINHA DE CÓDIGO:

1. *VERIFICAÇÃO OBRIGATÓRIA DE IDENTIFICADORES*: Use Grep para verificar se TODOS os identificadores (variáveis, funções, classes, IDs) que você pretende usar já existem no código. NUNCA declare um identificador sem verificar primeiro.

2. *VERIFICAÇÃO DE ESCOPO*: Se um identificador já existe no mesmo escopo/função, REUTILIZE-O. NUNCA redeclare variáveis no mesmo escopo.

3. *VERIFICAÇÃO DE FUNCIONALIDADE*: Use Grep para procurar funcionalidades similares antes de implementar algo novo. Se já existe, ESTENDA ou MODIFIQUE a funcionalidade existente.

4. *PROCESSO OBRIGATÓRIO*:
   - Primeiro: Grep para verificar identificadores
   - Segundo: Read para entender o contexto existente
   - Terceiro: Implementar APENAS se não existir
   - Quarto: Sempre usar cmake --build . -j$(nproc) 2>&1 && ctest --output-on-failure -j$(nproc) para compilar e testar

5. *PRINCÍPIO DA FONTE ÚNICA*: Uma funcionalidade = uma função. Um identificador = uma declaração por escopo. NUNCA duplique.

VIOLAÇÃO DESSAS REGRAS QUEBRA O SISTEMA E GERA ERROS DE SINTAXE.
