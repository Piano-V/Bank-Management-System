#include "bank.h"
#include <stdio.h>

int main(void) {
    if (db_init(1) != 0) {
        printf("Error: Failed to initialize database.\n");
        return 1;
    }

    printf("Database initialized successfully.\n\n");
    printf("Default test accounts:\n");
    printf("  Customer:  cust1  / pass   (ID: 101, Account: 10001, Balance: 5000)\n");
    printf("  Customer:  cust2  / pass2  (ID: 102, Account: 10002, Balance: 200)\n");
    printf("  Employee:  emp1   / pass   (ID: 201)\n");
    printf("  Manager:   mgr1   / pass   (ID: 301)\n");
    printf("  Admin:     admin1 / pass   (ID: 401)\n");
    return 0;
}
