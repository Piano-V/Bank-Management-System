#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include "bank.h"

#define SERVER_IP "127.0.0.1"

int current_user_id = -1;
int current_role_id = -1; // 1=Customer, 2=Employee, 3=Manager, 4=Admin
int current_account_id = -1;

void send_request(const char *request_message, char *response) {
    int sock_fd;
    struct sockaddr_in server_addr;
    ssize_t bytes_read;

    if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket failed");
        strcpy(response, "ERROR|Socket creation failed.");
        return;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("Address not supported");
        close(sock_fd);
        strcpy(response, "ERROR|Invalid server address.");
        return;
    }

    if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        strcpy(response, "ERROR|Connection refused. Is server running?");
        close(sock_fd);
        return;
    }

    send(sock_fd, request_message, strlen(request_message), 0);
    bytes_read = recv(sock_fd, response, BUFFER_SIZE - 1, 0);
    if (bytes_read > 0) {
        response[bytes_read] = '\0';
    } else {
        strcpy(response, "ERROR|Server closed connection.");
    }

    close(sock_fd);
}

void handle_client_signal(int sig) {
    (void)sig;
    if (current_user_id != -1 && current_role_id != -1) {
        char req[BUFFER_SIZE], res[BUFFER_SIZE];
        snprintf(req, sizeof(req), "LOGOUT|%d|%d", current_role_id, current_user_id);
        send_request(req, res);
        printf("\n\n[Client] Process interrupted. Logged out User %d gracefully.\n", current_user_id);
    } else {
        printf("\n\n[Client] Exiting...\n");
    }
    exit(0);
}

void clear_input_buffer() {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

int get_int_input(const char *prompt) {
    int val;
    printf("%s", prompt);
    if (scanf(" %d", &val) == 1) {
        clear_input_buffer();
        return val;
    }
    clear_input_buffer();
    return -1;
}

void get_str_input(const char *prompt, char *buffer, size_t max_len) {
    if (prompt && strlen(prompt) > 0) printf("%s", prompt);
    if (scanf("%49s", buffer) != 1) {
        buffer[0] = '\0';
    }
    (void)max_len;
    clear_input_buffer();
}

void customer_menu() {
    int choice;
    char request[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    char amount_str[50], target_acc_id_str[50], new_pass[50], feedback[256];
    char *token;

    while (current_user_id != -1) {
        if (current_account_id > 0) {
            printf("\n--- Customer Menu (User: %d | Account: %d) ---\n", current_user_id, current_account_id);
        } else {
            printf("\n--- Customer Menu (User: %d) ---\n", current_user_id);
        }
        printf("1. View Account Balance\n");
        printf("2. Deposit Money\n");
        printf("3. Withdraw Money\n");
        printf("4. Transfer Funds\n");
        printf("5. Apply for a Loan\n");
        printf("6. Change Password\n");
        printf("7. View Transaction History (Passbook)\n");
        printf("8. Adding Feedback\n");
        printf("9. Logout\n");
        printf("10. Exit\n");

        choice = get_int_input("Enter choice: ");

        switch (choice) {
            case 1:
                snprintf(request, BUFFER_SIZE, "VIEW_BALANCE|%d|%d", current_role_id, current_user_id);
                break;
            case 2:
                get_str_input("Enter amount to deposit: ", amount_str, sizeof(amount_str));
                snprintf(request, BUFFER_SIZE, "DEPOSIT|%d|%d|%s", current_role_id, current_user_id, amount_str);
                break;
            case 3:
                get_str_input("Enter amount to withdraw: ", amount_str, sizeof(amount_str));
                snprintf(request, BUFFER_SIZE, "WITHDRAW|%d|%d|%s", current_role_id, current_user_id, amount_str);
                break;
            case 4:
                get_str_input("Enter target account ID: ", target_acc_id_str, sizeof(target_acc_id_str));
                get_str_input("Enter amount to transfer: ", amount_str, sizeof(amount_str));
                snprintf(request, BUFFER_SIZE, "TRANSFER|%d|%d|%s|%s", current_role_id, current_user_id, target_acc_id_str, amount_str);
                break;
            case 5:
                get_str_input("Enter loan amount: ", amount_str, sizeof(amount_str));
                snprintf(request, BUFFER_SIZE, "APPLY_LOAN|%d|%d|%s", current_role_id, current_user_id, amount_str);
                break;
            case 6:
                get_str_input("Enter new password: ", new_pass, sizeof(new_pass));
                snprintf(request, BUFFER_SIZE, "CHANGE_PASS|%d|%d|%s", current_role_id, current_user_id, new_pass);
                break;
            case 7:
                snprintf(request, BUFFER_SIZE, "VIEW_HISTORY|%d|%d", current_role_id, current_user_id);
                break;
            case 8:
                printf("Enter feedback (max 255 chars): ");
                if (fgets(feedback, sizeof(feedback), stdin)) {
                    feedback[strcspn(feedback, "\r\n")] = 0;
                }
                snprintf(request, BUFFER_SIZE, "ADD_FEEDBACK|%d|%d|%s", current_role_id, current_user_id, feedback);
                break;
            case 9:
                snprintf(request, BUFFER_SIZE, "LOGOUT|%d|%d", current_role_id, current_user_id);
                send_request(request, response);
                current_user_id = -1;
                current_role_id = -1;
                current_account_id = -1;
                printf("Logged out successfully.\n");
                return;
            case 10:
                snprintf(request, BUFFER_SIZE, "LOGOUT|%d|%d", current_role_id, current_user_id);
                send_request(request, response);
                current_user_id = -1;
                current_role_id = -1;
                printf("Goodbye!\n");
                exit(0);
            default:
                printf("Invalid choice. Try again.\n");
                continue;
        }

        send_request(request, response);
        token = strtok(response, "|");
        if (token && strcmp(token, "SUCCESS") == 0) {
            printf(">> SUCCESS: ");
            token = strtok(NULL, "");
            if (token) {
                if (choice == 7) {
                    printf("\nDate & Time         | Type      | Amount   | Details\n");
                    printf("--------------------+-----------+----------+--------------------\n");
                    printf("%s\n", token);
                } else {
                    printf("%s\n", token);
                }
            } else {
                printf("Operation completed successfully.\n");
            }
        } else {
            printf(">> ERROR: ");
            token = strtok(NULL, "");
            if (token) printf("%s\n", token);
            else printf("Operation failed.\n");
        }
    }
}

void employee_menu() {
    int choice;
    char request[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    char username[50], password[50], balance_str[50], target_id_str[50], new_pass[50];
    char *token;

    while (current_user_id != -1) {
        printf("\n--- Employee Menu (User: %d) ---\n", current_user_id);
        printf("1. Add New Customer\n");
        printf("2. Modify Customer Details (Change Password)\n");
        printf("3. Process Loan Applications (Approve/Reject)\n");
        printf("4. View Customer Transactions (Passbook)\n");
        printf("5. View Assigned Loan Applications\n");
        printf("6. Change Password (Self)\n");
        printf("7. Logout\n");
        printf("8. Exit\n");
        choice = get_int_input("Enter choice: ");

        switch (choice) {
            case 1:
                get_str_input("Enter new Customer Username: ", username, sizeof(username));
                get_str_input("Enter new Password: ", password, sizeof(password));
                get_str_input("Enter Initial Balance: ", balance_str, sizeof(balance_str));
                snprintf(request, BUFFER_SIZE, "ADD_CUSTOMER|%d|%d|%s|%s|%s",
                         current_role_id, current_user_id, username, password, balance_str);
                break;
            case 2:
                get_str_input("Enter Customer User ID to modify: ", target_id_str, sizeof(target_id_str));
                get_str_input("Enter NEW Password: ", new_pass, sizeof(new_pass));
                snprintf(request, BUFFER_SIZE, "MOD_CUSTOMER|%d|%d|%s|%s",
                         current_role_id, current_user_id, target_id_str, new_pass);
                break;
            case 3:
                get_str_input("Enter Loan ID to process: ", target_id_str, sizeof(target_id_str));
                get_str_input("Enter status (APPROVED/REJECTED): ", new_pass, sizeof(new_pass));
                snprintf(request, BUFFER_SIZE, "PROCESS_LOAN|%d|%d|%s|%s",
                         current_role_id, current_user_id, target_id_str, new_pass);
                break;
            case 4:
                get_str_input("Enter Customer User ID for Passbook: ", target_id_str, sizeof(target_id_str));
                snprintf(request, BUFFER_SIZE, "VIEW_PASSBOOK|%d|%d|%s",
                         current_role_id, current_user_id, target_id_str);
                break;
            case 5:
                printf("Fetching assigned loans...\n");
                snprintf(request, BUFFER_SIZE, "VIEW_ASSIGNED_LOANS|%d|%d",
                         current_role_id, current_user_id);
                break;
            case 6:
                get_str_input("Enter new password: ", new_pass, sizeof(new_pass));
                snprintf(request, BUFFER_SIZE, "CHANGE_PASS|%d|%d|%s",
                         current_role_id, current_user_id, new_pass);
                break;
            case 7:
                snprintf(request, BUFFER_SIZE, "LOGOUT|%d|%d", current_role_id, current_user_id);
                send_request(request, response);
                current_user_id = -1;
                current_role_id = -1;
                printf("Logged out.\n");
                return;
            case 8:
                snprintf(request, BUFFER_SIZE, "LOGOUT|%d|%d", current_role_id, current_user_id);
                send_request(request, response);
                current_user_id = -1;
                current_role_id = -1;
                printf("Goodbye!\n");
                exit(0);
            default:
                printf("Invalid choice.\n");
                continue;
        }

        send_request(request, response);
        token = strtok(response, "|");
        if (token && strcmp(token, "SUCCESS") == 0) {
            printf(">> SUCCESS: ");
            token = strtok(NULL, "");
            if (token) {
                if (choice == 4) {
                    printf("\nDate & Time         | Type      | Amount   | Details\n");
                    printf("--------------------+-----------+----------+--------------------\n");
                    printf("%s\n", token);
                } else if (choice == 5) {
                    printf("\n");
                    printf("%s\n", token);
                } else {
                    printf("%s\n", token);
                }
            }
        } else {
            printf(">> ERROR: ");
            token = strtok(NULL, "");
            if (token) printf("%s\n", token);
        }
    }
}

void manager_menu() {
    int choice;
    char request[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    char target_id_str[50], employee_id_str[50], status_str[50], new_pass[50];
    char *token;

    while (current_user_id != -1) {
        printf("\n--- Manager Menu (User: %d) ---\n", current_user_id);
        printf("1. Activate/Deactivate Customer Accounts\n");
        printf("2. Assign Loan Application Processes to Employees\n");
        printf("3. Review Customer Feedback\n");
        printf("4. Change Password\n");
        printf("5. Logout\n");
        printf("6. Exit\n");
        choice = get_int_input("Enter choice: ");

        switch (choice) {
            case 1:
                get_str_input("Enter Customer User ID: ", target_id_str, sizeof(target_id_str));
                get_str_input("Enter new status (ACTIVATE/DEACTIVATE): ", status_str, sizeof(status_str));
                snprintf(request, BUFFER_SIZE, "ACT_DEACT_ACC|%d|%d|%s|%s",
                         current_role_id, current_user_id, target_id_str, status_str);
                break;
            case 2:
                printf("\n--- Fetching Pending Loans ---\n");
                snprintf(request, BUFFER_SIZE, "VIEW_LOANS|%d|%d", current_role_id, current_user_id);
                send_request(request, response);

                token = strtok(response, "|");
                if (token && strcmp(token, "SUCCESS") == 0) {
                    printf(">> Loan Status List:\n");
                    token = strtok(NULL, "");
                    if (token) printf("%s\n", token);
                } else {
                    printf(">> ERROR: Failed to retrieve loan list: %s\n", token ? strtok(NULL, "") : "Unknown.");
                    break;
                }

                printf("--- Loan Assignment ---\n");
                get_str_input("Enter Loan ID to assign: ", target_id_str, sizeof(target_id_str));
                get_str_input("Enter Employee ID: ", employee_id_str, sizeof(employee_id_str));
                snprintf(request, BUFFER_SIZE, "ASSIGN_LOAN|%d|%d|%s|%s",
                         current_role_id, current_user_id, target_id_str, employee_id_str);
                break;
            case 3:
                snprintf(request, BUFFER_SIZE, "REVIEW_FEEDBACK|%d|%d", current_role_id, current_user_id);
                break;
            case 4:
                get_str_input("Enter new password: ", new_pass, sizeof(new_pass));
                snprintf(request, BUFFER_SIZE, "CHANGE_PASS|%d|%d|%s",
                         current_role_id, current_user_id, new_pass);
                break;
            case 5:
                snprintf(request, BUFFER_SIZE, "LOGOUT|%d|%d", current_role_id, current_user_id);
                send_request(request, response);
                current_user_id = -1;
                current_role_id = -1;
                printf("Logged out.\n");
                return;
            case 6:
                snprintf(request, BUFFER_SIZE, "LOGOUT|%d|%d", current_role_id, current_user_id);
                send_request(request, response);
                current_user_id = -1;
                current_role_id = -1;
                printf("Goodbye!\n");
                exit(0);
            default:
                printf("Invalid choice.\n");
                continue;
        }

        send_request(request, response);
        token = strtok(response, "|");
        if (token && strcmp(token, "SUCCESS") == 0) {
            printf(">> SUCCESS: ");
            token = strtok(NULL, "");
            if (token) {
                if (choice == 3) {
                    printf("\n");
                    printf("%s\n", token);
                } else {
                    printf("%s\n", token);
                }
            }
        } else {
            printf(">> ERROR: ");
            token = strtok(NULL, "");
            if (token) printf("%s\n", token);
        }
    }
}

void administrator_menu() {
    int choice;
    char request[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    char username[50], password[50], target_id_str[50], new_pass[50], new_role_id_str[50];
    char *token;

    while (current_user_id != -1) {
        printf("\n--- Administrator Menu (User: %d) ---\n", current_user_id);
        printf("1. Add New Bank Employee\n");
        printf("2. Modify Customer/Employee Details\n");
        printf("3. Manage User Roles\n");
        printf("4. Change Password\n");
        printf("5. Logout\n");
        printf("6. Exit\n");
        choice = get_int_input("Enter choice: ");

        switch (choice) {
            case 1:
                get_str_input("Enter new Employee Username: ", username, sizeof(username));
                get_str_input("Enter new Password: ", password, sizeof(password));
                snprintf(request, BUFFER_SIZE, "ADD_EMPLOYEE|%d|%d|%s|%s",
                         current_role_id, current_user_id, username, password);
                break;
            case 2:
                get_str_input("Enter Target User ID: ", target_id_str, sizeof(target_id_str));
                get_str_input("Enter NEW Password: ", new_pass, sizeof(new_pass));
                snprintf(request, BUFFER_SIZE, "MOD_USER_DETAILS|%d|%d|%s|%s",
                         current_role_id, current_user_id, target_id_str, new_pass);
                break;
            case 3:
                get_str_input("Enter Target User ID: ", target_id_str, sizeof(target_id_str));
                get_str_input("Enter NEW Role ID (1=Cust, 2=Emp, 3=Mgr, 4=Adm): ", new_role_id_str, sizeof(new_role_id_str));
                snprintf(request, BUFFER_SIZE, "MANAGE_ROLES|%d|%d|%s|%s",
                         current_role_id, current_user_id, target_id_str, new_role_id_str);
                break;
            case 4:
                get_str_input("Enter new password: ", new_pass, sizeof(new_pass));
                snprintf(request, BUFFER_SIZE, "CHANGE_PASS|%d|%d|%s",
                         current_role_id, current_user_id, new_pass);
                break;
            case 5:
                snprintf(request, BUFFER_SIZE, "LOGOUT|%d|%d", current_role_id, current_user_id);
                send_request(request, response);
                current_user_id = -1;
                current_role_id = -1;
                printf("Logged out.\n");
                return;
            case 6:
                snprintf(request, BUFFER_SIZE, "LOGOUT|%d|%d", current_role_id, current_user_id);
                send_request(request, response);
                current_user_id = -1;
                current_role_id = -1;
                printf("Goodbye!\n");
                exit(0);
            default:
                printf("Invalid choice.\n");
                continue;
        }

        send_request(request, response);
        token = strtok(response, "|");
        if (token && strcmp(token, "SUCCESS") == 0) {
            printf(">> SUCCESS: ");
            token = strtok(NULL, "");
            if (token) printf("%s\n", token);
        } else {
            printf(">> ERROR: ");
            token = strtok(NULL, "");
            if (token) printf("%s\n", token);
        }
    }
}

int main() {
    signal(SIGINT, handle_client_signal);
    signal(SIGHUP, handle_client_signal);
    signal(SIGTERM, handle_client_signal);

    int role_choice;
    char username[50], password[50];
    char request[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    char *token;

    printf("\n--- Banking Management System ---\n");

    while (1) {
        if (current_user_id == -1) {
            printf("\n--- Select Role to Login ---\n");
            printf("1. Customer\n");
            printf("2. Bank Employee\n");
            printf("3. Manager\n");
            printf("4. Administrator\n");
            printf("5. Exit\n");
            role_choice = get_int_input("Enter choice: ");

            if (role_choice == 5) {
                printf("Exiting application.\n");
                break;
            }
            if (role_choice < 1 || role_choice > 4) {
                printf("Invalid role choice. Please select 1 to 5.\n");
                continue;
            }

            if (role_choice == 1) {
                printf("\n--- Customer Access ---\n");
                printf("1. Existing Account Login\n");
                printf("2. Create New Account (Register)\n");
                int customer_choice = get_int_input("Enter choice: ");

                if (customer_choice == 2) {
                    printf("\n--- New Customer Registration ---\n");
                    get_str_input("Enter desired Username (max 49 chars): ", username, sizeof(username));
                    get_str_input("Enter Password: ", password, sizeof(password));

                    snprintf(request, BUFFER_SIZE, "CREATE_CUSTOMER|%d|%s|%s", role_choice, username, password);
                    send_request(request, response);

                    token = strtok(response, "|");
                    if (token != NULL && strcmp(token, "SUCCESS") == 0) {
                        token = strtok(NULL, "|");
                        int new_uid = token ? atoi(token) : -1;
                        token = strtok(NULL, "|");
                        int new_acc = token ? atoi(token) : -1;
                        if (new_acc > 0) {
                            printf(">> Account created successfully! User ID: %d | Account Number: %d\n", new_uid, new_acc);
                        } else {
                            printf(">> Account created successfully! Your User ID is: %d\n", new_uid);
                        }
                        printf("Please log in with your new credentials.\n");
                        continue;
                    } else {
                        token = strtok(NULL, "|");
                        printf(">> Account Creation Failed: %s\n", token ? token : "Server error.");
                        continue;
                    }
                } else if (customer_choice != 1) {
                    printf("Invalid choice. Returning to role selection.\n");
                    continue;
                }
            }

            printf("\nEnter Username: ");
            get_str_input("", username, sizeof(username));
            printf("Enter Password: ");
            get_str_input("", password, sizeof(password));

            snprintf(request, BUFFER_SIZE, "LOGIN|%d|%s|%s", role_choice, username, password);
            send_request(request, response);

            token = strtok(response, "|");
            if (token != NULL && strcmp(token, "SUCCESS") == 0) {
                token = strtok(NULL, "|");
                current_user_id = token ? atoi(token) : -1;
                token = strtok(NULL, "|");
                current_role_id = token ? atoi(token) : role_choice;
                token = strtok(NULL, "|");
                current_account_id = token ? atoi(token) : -1;
                if (current_account_id > 0) {
                    printf(">> Login successful! Welcome User ID %d | Account Number: %d\n", current_user_id, current_account_id);
                } else {
                    printf(">> Login successful! Welcome User ID %d (Role %d)\n", current_user_id, current_role_id);
                }
            } else {
                token = strtok(NULL, "|");
                printf(">> Login Failed: %s\n", token ? token : "Unknown error.");
            }
        }

        if (current_user_id != -1) {
            switch (current_role_id) {
                case 1: customer_menu(); break;
                case 2: employee_menu(); break;
                case 3: manager_menu(); break;
                case 4: administrator_menu(); break;
            }
        }
    }
    return 0;
}
