#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "bpt.h"


/*Example:
create table teste3 (int a pk, char[100] b)
insert int teste3 values ('aaaa')
insert int teste3 values ('aaaa')
select * from teste3
*/


// estrutura de cabeçalho
typedef struct Header {
    int memFree; // limpar memoria da pagina
    int next; // indica onde sera inserido o proximo elemento na página
    int qtdItems; // quantidade de itens
} header;

typedef struct Item {
    int offset;
    int totalLen; // tamanho total do elemento 
    int writed; 
} item;

typedef struct Attribute { // atributo da tabela sql, coluna da tabela sql
    char name[15]; // nome da coluna da tabela sql
    int size; // tamanho da coluna da tabela sql
    char type; //tipo da coluna da tabela sql = int, char e varchar
    int pk; // define se o atributo é pk
    int ai; // define se o atributo é ai
} attribute;

// 0 - Oculta debug
// 1 - Habilita debug
int debug = 0;

void getTableName(char *sql, char *name);

int buildHeader(char *sql, char *tableName, int qtdPages);

void revertTableCreation(char *tableName);

void generatePkFile(char *tableName, char *fieldName);

void getAllAtributes(char *sql, char *attributes);

void leArquivo(char *tableName);

node *loadTableBPT(node *root, char *tableName); 

void saveTableBPT(node *root, char *tableName);

int extreactPkValueFromSQL(char *sql);

/**
 * Separa a operação do restante da string SQL 
 * - select ou
 * - create ou
 * - insert ou
 * - delete
 */
void getOp(char *sql, char *operation) {
    char sqlCopy[1000], *token;
    char key[2] = " ";
    char *ptr;

    memset(sqlCopy, '\0', sizeof(sqlCopy));

    strcpy(sqlCopy, sql);

    token = strtok(sqlCopy, key);

    strcpy(operation, token);
    
    if((ptr = strchr(operation, '\n')) != NULL)
        *ptr = '\0';
}

void createPage(char *tableName, int numPage) {
    //cria pagina no disco com tamanho de 8kb
  	header head;
    head.memFree = 8180; // quantidade de memória livre na página
	head.next = 8191; // usado na criação do arquivo da pagina, para indicar onde sera inserido o proximo elemento
    head.qtdItems = 0; //usado na criação do arquivo da pagina, incialemnte cria a pagina zerada

  	struct stat stateDir = {0};

    char pageName[600]; 

    //printf("tableName: %s\n\n", tableName);
    //printf("NumPage: %d\n\n", numPage);

  	//define o nome da pagina na variavel pageName, com base nos parametros informados na função
    snprintf(pageName, sizeof(pageName), "%s/page%d.dat", tableName, numPage); 

    FILE *page = fopen(pageName, "wb"); //cria o arquivo da pagina no modo de escrita binária
    fwrite(&head.memFree, sizeof(int), 1, page);  // free
    fwrite(&head.next, sizeof(int), 1, page);     // onde inserir o proximo elemento
    fwrite(&head.qtdItems, sizeof(int), 1, page);// n elementos
    fclose(page); // fecha o arquivo
    
  	if(page == NULL) { //caso tenha ocorrido algum erro na criação da pagina
        printf("Failed to create page\n");
        return;
    }
}


void createTable(char *sql) {
	// cria cabeçalho da página
    header head;
    head.memFree = 8180; // 8kb
    head.next = 8191; // próxima página
    head.qtdItems = 0; // qtd de registros da tabela

	// delimitador de fim da página
    char special = '0';

    struct stat stateDir = {0};

	char tableName[500]; // define nome da tabela com 500 caracteres
  	char pageName[600]; // define nome da página com 500 caracteres
  	memset(tableName, '\0', sizeof(tableName)); // '\0' apos nome da tabela

	getTableName(sql, tableName); // pega o nome da tabela

	// status do arquivo -1 = tabela não existe
  	if(stat(tableName, &stateDir) == -1) {
        
		// cada tabela do banco é armazenada em um diretório com seu nome
      	if(mkdir(tableName, 0777) == 0)
            printf("Created table %s\n", tableName);
		 
        // cria arquivo do cabeçalho da tabela
        snprintf(pageName, sizeof(pageName), "%s/header.dat", tableName);
      	FILE *headerPage = fopen(pageName, "wb");
        fclose(headerPage);
        
	    // cria arquivo de página da tabela
      	snprintf(pageName, sizeof(pageName), "%s/page1.dat", tableName);  
      	FILE *page = fopen(pageName, "wb");
        fwrite(&head.memFree, sizeof(int), 1, page);  // quantidade de memória disponível
        fwrite(&head.next, sizeof(int), 1, page);     // onde inserir o proximo elemento
        fwrite(&head.qtdItems, sizeof(int), 1, page); // n elementos
        
      	// insere caracter especial ao final da página para indicar fim de página
      	// especial = '0'
      	fseek(page, 8191, SEEK_SET);
        fwrite(&special, 1, 1, page);
      	fclose(page);
        
      	if(page == NULL) {
            printf("Failed to create page\n");
            return;
        }
	
    	if(buildHeader(sql, tableName, 1) == 1){
            // a tabela eh invalida e deve ser revertida
            revertTableCreation(tableName);
        }
    } else {
        printf("Table %s already exist\n", tableName);
    }
}

void revertTableCreation(char *tableName){
  	char pageName[600]; // define nome da página com 500 caracteres

    // Deleta header
    snprintf(pageName, sizeof(pageName), "%s/header.dat", tableName);
    remove(pageName);

    // Deleta pagina
    snprintf(pageName, sizeof(pageName), "%s/page1.dat", tableName);  
    remove(pageName);

    rmdir(tableName);

    if(debug) printf("Table %s was deleted\n", tableName);
}

int buildHeader(char *sql, char *tableName, int qtdPages) {
    int invalidTable = 0; // flag principal, se retornar 1, a tabela eh revertida
    char fieldName[15], pkFieldName[15], fieldType; //nome e tipo do campo
    char matchPKField[18], matchAIField[21]; // armazena a str de busca para verificar PK ou AI
    int fieldSize, isPkField, isAiField, i = 0; //tamanho do campo e variavel auxiliar
    int isValidField = 0, isDynamicSizeType = 0, pkDefined = 0; // flags
    int initialAiValue = 0;

    char sqlCopy[1000], attribute[50], pageName[600];  
    char *token, *tokenAttribute;

    memset(fieldName, '\0', sizeof(fieldName));  //limpa a string do field name

    strcpy(sqlCopy, sql); //cria um arquivo auxiliar para o script sql

    snprintf(pageName, sizeof(pageName), "%s/header.dat", tableName); //define o nome do arquvio de cabeçalho
    FILE *headerPage = fopen(pageName, "rb+"); //instancia o arquvio de cabeçalho em modo de escrita e leitura

    fwrite(&i, sizeof(int), 1, headerPage); //escreve o conteudo de i (0) no inicio do arquivo de cabeçalho   

	// encontra o primeiro parentese na string SQL para e separa em tokens
	// após o primeiro parentese estarão as definições de atributos da tabela
  	// ex: ( int id, varchar nome )
    token = strtok(sqlCopy, "(");  
    token = strtok(NULL, "()[], "); 

    while(token != NULL) { // enquanto existir tokens
        isValidField = 0;
        isDynamicSizeType = 0;
        isPkField = 0;
        isAiField = 0;

        if(strcmp(token, "char") == 0) { // compara se o token é um char
            isValidField = 1;
            isDynamicSizeType = 1;
            fieldType = 'C'; // define o tipo do atributo como C
        } else if(strcmp(token, "int") == 0) { // compara se é int
            isValidField = 1;
            fieldType = 'I'; // tipo do atributo 
            token = strtok(NULL, "()[], ");
            fieldSize = sizeof(int); // tamanho do atributo igual ao tamanho de um inteiro, 4bytes
        } else if(strcmp(token, "varchar") == 0) { // compara se é varchar
            isValidField = 1;
            isDynamicSizeType = 1;
            fieldType = 'V'; // tipo do atributo 
        }

        if(isValidField){
            // Field Type
            fwrite(&fieldType, 1, 1, headerPage); 
            if(isDynamicSizeType){
                token = strtok(NULL, "()[], ");
                fieldSize = atoi(token);
                token = strtok(NULL, "()[], ");
            }
            if(debug) printf("Type: %c\n", fieldType);

            // Filed Size
            fwrite(&fieldSize, sizeof(int), 1, headerPage);
            if(debug) printf("Size: %d\n", fieldSize);

            // Field Name
            strcpy(fieldName, token);
            fwrite(fieldName, 15, 1, headerPage);
            if(debug) printf("Name: %s\n", fieldName);
            
            // Field Primary Key
            snprintf(matchPKField, sizeof(matchPKField), "%s pk", fieldName); 
            if(strstr(sql, matchPKField)){
                isPkField = 1;
                if(isDynamicSizeType){
                    printf("Primary Key must to be an integer type\n");
                    invalidTable = 1;
                    break;
                }
                if(pkDefined){
                    printf("Primary Key is already defined\n");
                    invalidTable = 1;
                    break;
                }
                if(i != 0){
                    printf("Primary Key must to be the first field\n");
                    invalidTable = 1;
                    break;
                }
                pkDefined = 1;
                strcpy(pkFieldName, fieldName);

                snprintf(matchAIField, sizeof(matchAIField), "%s pk ai", fieldName); 
                if(strstr(sql, matchAIField)){
                    isAiField = 1;
                }
            }

            fwrite(&isPkField, sizeof(int), 1, headerPage);
            fwrite(&isAiField, sizeof(int), 1, headerPage);
            if(debug) printf("Primary Key: %s\n", isPkField ? "true" : "false");
            if(debug) printf("Auto Increment: %s\n", isAiField ? "true" : "false");

            i++;
        }

        token = strtok(NULL, "()[], "); // procura o fechamento do script create table
    }

    fwrite(&initialAiValue, sizeof(int), 1, headerPage); // escreve o valor inicial do ai
    fwrite(&qtdPages, sizeof(int), 1, headerPage); // escreve no arquivo de cabeçalho, a quantidade de paginas daquela tabela 
    fseek(headerPage, 0, SEEK_SET); // move o ponteiro do arquivo para o inicio
    fwrite(&i, sizeof(int), 1, headerPage); // escreve o numero de atributos daquela tabela
    fclose(headerPage); // fecha o arquivo de cabeçalho

	leArquivo(tableName); // le o arquivo daquela tabela

    if(pkDefined) {
        generatePkFile(tableName, pkFieldName);
    }

    return invalidTable;
}

void generatePkFile(char *tableName, char *fieldName){
    char pkFileName[600];  
    int idCount = 0;

    snprintf(pkFileName, sizeof(pkFileName), "%s/pk.dat", tableName); //define o nome do arquivo da pk
    
    if(debug) printf("PK file created at: %s\n", pkFileName);

    FILE *filePk = fopen(pkFileName, "wb"); //instancia o arquvio de cabeçalho em modo de escrita e leitura
    fwrite(&idCount, sizeof(int), 1, filePk); 
    fclose(filePk); // fecha o arquivo de cabeçalho
}

int extreactPkValueFromSQL(char *sql){
    char *token;
    
    // Busca o primeiro valor, pois a PK sempre vai ser
    // o primeiro campo da tabela
    token = strtok(sql, "(,"); // procura os valores da inserção
    token = strtok(NULL, "(,"); // continua a busca de onde parou na chamada acima

    if(token == NULL)
        return 0;

    return atoi(token);
}

void insertInto(char *sql, int numPage) { 
    char sqlCopy[1000], sqlExtractPK[1000], *token, tableName[500], pageName[600], headerName[600], attrSql[1000], attrSqlCopy[1000];
    char endVarchar = '$', special = ' ', endChar = '\0';
    int insertSize = 0, qtdFields, nextItem, intVar, nextPage, qtdEndChar = 0;
    int i = 0, countVarchar = 0, pkInserted;
    int pkFieldExist = 0, aiFieldExist = 0, aiValue = 0;
    attribute attributes[64];
    header head;
    node *root = NULL;

    memset(sqlCopy, '\0', sizeof(sqlCopy)); //limpa a variavel que sera usada na copia do script sql
    strcpy(sqlCopy, sql); // script sql
    token = strtok(sqlCopy, " () ,"); // quebra o script sql em tokens

    while(token != NULL) { // enquanto existirem tokens
        // quando o i for igual a 2, o token vai ser o nome da tabela
        if(i == 2) 
            strcpy(tableName, token);
        token = strtok(NULL, " () ,");
        i++;
    }

    snprintf(headerName, sizeof(headerName), "%s/header.dat", tableName); // define o caminho para o cabeçalho da tabela
    FILE *headerPage = fopen(headerName, "rb+"); // abre o arquivo de cabeçalho
 
    if(!headerPage) { // caso nao consiga abrir
        printf("Table '%s' doesn't exist\n", tableName);
        return;
    }

    // Carrega o a arvore
    root = loadTableBPT(root, tableName);

	// lê a quantidade de colunas da tabela
  	fread(&qtdFields, sizeof(int), 1, headerPage);
    if(debug) printf("Fileds count: %d\n", qtdFields);

	// laço para ler do cabeçalho da tabela: o nome, tamanho e tipo das colunas
    for(int i = 0; i < qtdFields; i++) {
        fread(&attributes[i].type, 1, 1, headerPage);
        fread(&attributes[i].size, sizeof(int), 1, headerPage);
        fread(attributes[i].name, 15, 1, headerPage);
        fread(&attributes[i].pk, sizeof(int), 1, headerPage);
        fread(&attributes[i].ai, sizeof(int), 1, headerPage);

        if(attributes[i].pk){
            pkFieldExist = 1;
            if(debug) printf("Table has primary key: %s\n", attributes[i].name);

            if(attributes[i].ai){
                aiFieldExist = 1;
                if(debug) printf("Primary key %s is AI\n", attributes[i].name);
            }
        }
    }
	
    // le o valor de auto increment
    fread(&aiValue, sizeof(int), 1, headerPage);

    // incrementa o controle do ai e atualiza o cabecalho
    if(aiFieldExist){
        aiValue++;
        if(debug) printf("Next primary key value: %d\n", aiValue);
        fseek(headerPage, -sizeof(int), SEEK_CUR);
        fwrite(&aiValue, sizeof(int), 1, headerPage);
    } else if(pkFieldExist == 1) {
        if(debug) printf("Busca se chave já existe\n");
        
        memset(sqlExtractPK, '\0', sizeof(sqlExtractPK));
        strcpy(sqlExtractPK, sql); 

        pkInserted = extreactPkValueFromSQL(sqlExtractPK);
        record * recordPk = find(root, pkInserted, false, NULL);

        if(recordPk != NULL){
            printf("Cannot duplicate a PK value\n");
            return;
        }
    }
    
  	// procura no arquivo o caminho da proxima pagina
    fread(&nextPage, sizeof(int), 1, headerPage);
    if(debug) printf("Next page value: %d\n", nextPage);
  	fclose(headerPage); // fecha o cabeçalho
    
    strcpy(sqlCopy, sql); // faz uma cópia do script sql
    token = strtok(sqlCopy, "()"); // procura os valores da inserção
    token = strtok(NULL, "()"); // continua a busca de onde parou na chamada acima

    memset(attrSql, '\0', sizeof(attrSql)); //limpa a variavel e seta o final dela
    memset(attrSqlCopy, '\0', sizeof(attrSqlCopy)); // limpa a variável e seta o final dela

    strcpy(attrSql, token); //copia o token de atributo para a variavel attrSql

    strcpy(attrSqlCopy, attrSql); // faz uma copia da variavel attrSql

    token = strtok(attrSqlCopy, ","); // quebra o token de atributos usando o delimitador de virgula

   	// incrementa o tamanho do insert baseado no tipo dos atributos inseridos
  	for(int i = 0; i < qtdFields; i++) {     	
      	if(attributes[i].type == 'C') {
            insertSize += attributes[i].size;
        } else if(attributes[i].type == 'I') {
            insertSize += attributes[i].size;
        } else if(attributes[i].type == 'V') {
            insertSize += strlen(token) + 1; // + 1 for special char of varchar '$'
            //countVarchar
        }
        token = strtok(NULL, ",");
    }

    strcpy(attrSqlCopy, attrSql); 

    snprintf(pageName, sizeof(pageName), "%s/page%d.dat", tableName, numPage); //define o caminho da pagina com os registros da tabela
    FILE * page = fopen(pageName, "rb+"); // abre a pagina como modo de leitura e escrita binaria

    fread(&head.memFree, sizeof(int), 1, page); // quanto de espaço livre tem disponível naquela pagina
    fread(&head.next, sizeof(int), 1, page); // caminho da proxima pagina
    fread(&head.qtdItems, sizeof(int), 1, page); // quantidade de itens naquela pagina

  	// se valores inseridos forem menores que o espaço livre na página, insere na mesma página
    if(head.memFree > insertSize) {
        token = strtok(attrSqlCopy, ",");
        item newItem;
        newItem.offset = head.next - insertSize;
        newItem.totalLen = insertSize;
        newItem.writed = 1;

      	// calcula posição do próximo valor no cabeçalho da página 
        nextItem = 12 + 12 * head.qtdItems;
		// move ponteiro do arquivo para posição do próximo valor no cabeçalho da página
        fseek(page, nextItem, SEEK_SET);

		// insere informações do insert no cabeçalho da página
      	fwrite(&newItem.offset, sizeof(int), 1, page);
        fwrite(&newItem.totalLen, sizeof(int), 1, page);
        fwrite(&newItem.writed, sizeof(int), 1, page);
	
		// move ponteiro para posição onde dados do
        // insert serão inseridos na página
        fseek(page, newItem.offset, SEEK_SET);

        //printf("OFFSET NEW ITEM: %d\n", newItem.offset);

      	// percorre os campos do insert
        int pkValue = 0;
        for(int i = 0; i < qtdFields; i++) {
            if(attributes[i].pk && attributes[i].ai){
                fwrite(&aiValue, attributes[i].size, 1, page);
                pkValue = aiValue;

            // char
      		} else if(attributes[i].type == 'C') {
                fwrite(token, strlen(token), 1, page);
                qtdEndChar = attributes[i].size - strlen(token);
                fwrite(&endChar, 1, qtdEndChar, page);

            // int
            } else if(attributes[i].type == 'I') {
                intVar = atoi(token);
                fwrite(&intVar, attributes[i].size, 1, page);
                if(attributes[i].pk) {
                    pkValue = intVar;
                }

            // varchar
            } else if(attributes[i].type == 'V') {
                fwrite(token, strlen(token), 1, page);
                fwrite(&endVarchar, 1, 1, page);
            }
            
            // pula a extração do valor quando possui ai
            if((aiFieldExist && i > 0) || !aiFieldExist) {
                token = strtok(NULL, ",");
            }
        }

        // adicione o id na B+
        if(pkFieldExist){
            root = insert(root, pkValue, numPage, newItem.offset);
            if(debug) printf("Inserindo info da chave %d: pag->%d offset->%d\n", pkValue, numPage, newItem.offset);
        }
        
        head.memFree = head.memFree - insertSize;
        head.next = newItem.offset;
        head.qtdItems += 1;

        fseek(page, 0, SEEK_SET);

        fwrite(&head.memFree, sizeof(int), 1, page);
        fwrite(&head.next, sizeof(int), 1, page);
        fwrite(&head.qtdItems, sizeof(int), 1, page);

        fclose(page);

        // salva a arvore
        saveTableBPT(root, tableName);

        printf("New item inserted\n");
    } else {
      	// cria uma nova pagina
        fseek(page, 8191, SEEK_SET);
        fread(&special, 1, 1, page);
        //printf("Special: %c\n", special);
        if(special == '0') {
            //printf("Entrou special\n");
            nextPage++;
            //printf("Nome Pagina: %s\n", tableName);
            //printf("Numero pagina: %d\n", nextPage);
            //printf("SQL - %s\n", sql);
            createPage(tableName, nextPage);
            special = '1';
            fseek(page, 8191, SEEK_SET);
            fwrite(&special, 1, 1, page);
            fclose(page);
            insertInto(sql, nextPage);
        } else {
            fclose(page);
            insertInto(sql, nextPage);
        }
    }
}

/**
 *	retorna o nome da tabela
 */
void getTableName(char *sql, char *name) {
    char sqlCopy[1000];
    char *token;
    char keyParen[2] = "(";
    char keySpace[2] = " ";

    memset(sqlCopy, '\0', sizeof(sqlCopy));

    strcpy(sqlCopy, sql);

    token = strtok(sqlCopy, keyParen);

    strcpy(sqlCopy, token);

    token = strtok(sqlCopy, keySpace);

    while(token != NULL) {
        strcpy(name, token);
        token = strtok(NULL, keySpace);
    }
}

/**
 * lê cabeçalho da primeira página da tabela
 */
void leArquivo(char *tableName) {
    char pageName[600];
    char pageCoisa[600];

    snprintf(pageCoisa, sizeof(pageCoisa), "%s/page1.dat", tableName); //define o caminho da pagina de determinada tabela

    FILE *page = fopen(pageCoisa, "rb+"); // abre a pagina da tabela como leitura e escrita binária

    header head; 

    fread(&head.memFree, sizeof(int), 1, page);
    fread(&head.next, sizeof(int), 1, page);
    fread(&head.qtdItems, sizeof(int), 1, page);
    //printf("MemFree - %d; Next - %d - QtdItems - %d\n", head.memFree, head.next, head.qtdItems);

    fclose(page); // fecha a página
}

void saveTableBPT(node * const root, char *tableName){
    // print_leaves(root);
    char pkFile[600];
    int idCount = 0, i;
    node *c = root;
    record *data = NULL;

    snprintf(pkFile, sizeof(pkFile), "%s/pk.dat", tableName); 
    FILE *fp = fopen(pkFile, "wb"); 

    if(fp == NULL){
        return;
    }
    fwrite(&idCount, sizeof(int), 1, fp);

    if(root != NULL){
        while (!c->is_leaf)
            c = c->pointers[0];

        while (true){
            for (i = 0; i < c->num_keys; i++){
                data = (record *)c->pointers[i];
                if(debug) printf("Inserindo no arquivo da B+ key(%d) page(%d) offset(%d)\n", c->keys[i], data->page, data->offset);
                fwrite(&c->keys[i], sizeof(int), 1, fp);
                fwrite(&data->page, sizeof(int), 1, fp);
                fwrite(&data->offset, sizeof(int), 1, fp);
                idCount++;
            }
            if (c->pointers[DEFAULT_ORDER - 1] != NULL) {
            }
            else
                break;
        }
    }

    fseek(fp, 0, SEEK_SET);
    fwrite(&idCount, sizeof(int), 1, fp);
    fclose(fp);

    destroy_tree(root);
}

node *loadTableBPT(node *root, char *tableName){
    char pkDataFIle[600];
    int key, page, offset, idCount, i;

    snprintf(pkDataFIle, sizeof(pkDataFIle), "%s/pk.dat", tableName); //define o caminho da pagina de determinada tabela

    FILE *fp = fopen(pkDataFIle, "r"); // abre a pagina da tabela como leitura e escrita binária

    if(fp != NULL){
        // Le a quantidade de registros
        fread(&idCount, sizeof(int), 1, fp);

        if(idCount > 0){
            for (i = 0; i < idCount; i++) {
                fread(&key, sizeof(int), 1, fp);
                fread(&page, sizeof(int), 1, fp);
                fread(&offset, sizeof(int), 1, fp);
                root = insert(root, key, page, offset);
            }
        }
        if(debug) printf("Pk da tabela %s foi carregada com sucesso\n", tableName);
    } else {
        if(debug) printf("Pk da tabela %s não existe\n", tableName);
    }

    fclose(fp);

    return root;
}


void selectFrom(char *sql, int numPage) {
    char tableName[50], sqlCopy[1000], pageName[600], *token, charInFile, special = ' ';
    int i = 0, qtdFields = 0, moveItem = 0, moveTupla = 0, intInFile, stopChar;

    item readItem;
    attribute attributes[64];
    memset(sqlCopy, '\0', sizeof(sqlCopy));

    strcpy(sqlCopy, sql);

    token = strtok(sqlCopy, " "); // separa o script em sql usando o espaço como delimitador

    while(token != NULL) { // procura o nome da tabela
        // Se i == 3 então o token vai ser igual o nome da tabela
        if(i == 3) {
            token = strtok(token, "\n");
            strcpy(tableName, token);
        } else
            token = strtok(NULL, " "); 
        i++;
    }

    //printf("\nTable selected - %s\n", tableName);

    snprintf(pageName, sizeof(pageName), "%s/header.dat", tableName); // procura o arquivo do cabeçalho da tabela

    FILE *headerPage = fopen(pageName, "rb+"); // abre o arquivo do cabeçalho da tabela
    if(!headerPage) { // caso nao consiga abrir
        printf("Table '%s' doesn't exist\n", tableName);
        return;
    }

    fread(&qtdFields, sizeof(int), 1, headerPage); // le a quantidade de campos

    //printf("Quantidade de campos: %d\n", qtdFields);

    for(int i = 0; i < qtdFields; i++) { // laço percorrendo a quantidade de campos (sempre vai imprimir todos os campos)
        fread(&attributes[i].type, 1, 1, headerPage); // le o tipo do campo
        fread(&attributes[i].size, sizeof(int), 1, headerPage); // le o tamanho do campo
        fread(attributes[i].name, 15, 1, headerPage); // lê o nome do campo no cabeçalho
        printf("%s\t", attributes[i].name); // printa o nome do campo + tab
        fread(&attributes[i].pk, sizeof(int), 1, headerPage);
        fread(&attributes[i].ai, sizeof(int), 1, headerPage);
    }
    printf("\n");

    fclose(headerPage);

    snprintf(pageName, sizeof(pageName), "%s/page%d.dat", tableName, numPage); // define o nome da pagina da tabela
    FILE *page = fopen(pageName, "rb+"); // abre a pagina da tabela em modo de leitura e escrita binaria

    header head;

    item items;
    fread(&head.memFree, sizeof(int), 1, page); // verifica o espaço disponivel da pagina
    fread(&head.next, sizeof(int), 1, page); // verifica onde termina a pagina
    fread(&head.qtdItems, sizeof(int), 1, page); // verifica o numero de registros da pagina

    //printf("memFree: %d - next: %d - qtdItems: %d\n", head.memFree, head.next, head.qtdItems);

    for(int i = 0; i < head.qtdItems; i++) { // laço para percorrer os itens
        fseek(page, 0, SEEK_SET); // posiciona no inicio da pagina
        moveItem = sizeof(item) * (i + 1); // define o tamanho de cada registro
        fseek(page, moveItem, SEEK_SET); // vai para o final do registro 

        fread(&readItem.offset, sizeof(int), 1, page); // le o offset do registro
        fread(&readItem.totalLen, sizeof(int), 1, page); // tamanho total do registro
        fread(&readItem.writed, sizeof(int), 1, page); // se tem algo escrito naquele registro

        //printf("offset: %d - totalLen: %d - writed: %d\n", readItem.offset, readItem.totalLen, readItem.writed);

        if(readItem.writed == 0) // se o writed estiver setado como 0, então naquele registro nada foi escrito ainda
            continue;

        fseek(page, readItem.offset, SEEK_SET); // posiciona no offset do item

      	// percorre os campos da tabela
        for(int j = 0; j < qtdFields; j++) {  
			// se o atributo for char
          	if(attributes[j].type == 'C') { 
                charInFile = ' '; // seta a variavel com espaço em branco
                stopChar = 0; // delimitador indicando se chegou no fim do campo char
                for(int k = 0; k < attributes[j].size; k++) { //laço para percorer o registro do char
                    fread(&charInFile, 1, 1, page); 
                    if(charInFile == '\0') // caracter indicando o final do campo char
                        stopChar = 1; 
                    if(!stopChar) // caso nao tenha chegado no fim do arquivo
                        printf("%c", charInFile); //imprime caracter por caracter
                }
                printf("\t"); // imprime tab

          	// se o atributo for int
            } else if(attributes[j].type == 'I') {
                fread(&intInFile, sizeof(int), 1, page);
                printf("%d\t", intInFile);
            // se o atributo for varchar
            } else if(attributes[j].type == 'V') {
                charInFile = ' ';
              	// printa caracater a caracter até encontrar $, q delimita o fim de varchar
                do {
                    fread(&charInFile, 1, 1, page);
                    if(charInFile != '$')
                        printf("%c", charInFile);
                } while(charInFile != '$');
                printf("\t");
            }
        }
        printf("\n");
    }
    fseek(page, 8191, SEEK_SET); //posiciona no final do arquivo 
    fread(&special, 1, 1, page); // le o caracter special
    if(special == '1') { // se for igual a 1, significa que ainda existe pagina
        numPage++; 
        selectFrom(sql, numPage); //continua o select na(s) proxima(s) pagina(s)
    } else
        fclose(page); // fecha a pagina
}




int main() {
    char sql[1000], operation[10], attributes[500];

    do {
        printf(">> ");
        fgets(sql, 1000, stdin);
        getOp(sql, operation);

        if(strcmp(operation, "create") == 0) {
            createTable(sql);
        } else if(strcmp(operation, "insert") == 0) {
            insertInto(sql, 1);
        } else if(strcmp(operation, "select") == 0) {
            selectFrom(sql, 1);
        } else if(strcmp(operation, "quit") != 0) {
            printf("Cannot find '%s'\n", operation);
        }
    } while(strcmp(operation, "quit") != 0);

    return 0;
}
