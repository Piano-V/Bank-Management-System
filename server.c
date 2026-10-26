#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "bank.h"

#define BACKLOG    10
#define MAX_TOKENS 10

/* Handle dead child processes */

void sigchld_handler(int s) {
    (void)s;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

/* Process client request */

void handle_child_process(int client_fd) {
    char buffer[BUFFER_SIZE] = {0};
    char response[BUFFER_SIZE] = "ERROR|Unknown Request.";
    ssize_t bytes_read = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);

    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        printf("[Worker PID %d] Request: '%s'\n", getpid(), buffer);

        char *tokens[MAX_TOKENS];
        int i = 0;
        tokens[i] = strtok(buffer, "|");
        while (tokens[i] != NULL && i < MAX_TOKENS - 1) {
            tokens[++i] = strtok(NULL, "|");
        }
        int num_tokens = i;

        if (num_tokens >= 3) {
            const char *action = tokens[0];
            int role_id = atoi(tokens[1]);
            int user_id = atoi(tokens[2]);

            // Validate active session for all actions except LOGIN, LOGOUT, CREATE_CUSTOMER
            if (strcmp(action, "LOGIN") != 0 && strcmp(action, "LOGOUT") != 0 && strcmp(action, "CREATE_CUSTOMER") != 0) {
                if (!db_check_session(user_id)) {
                    strcpy(response, "ERROR|SESSION_EXPIRED");
                    goto send_and_exit;
                }
            }

            // Universal Auth Operations
            if (strcmp(action, "LOGOUT") == 0 && num_tokens == 3) {
                db_logout(user_id);
                strcpy(response, "SUCCESS|Session terminated.");
            } else if (strcmp(action, "CREATE_CUSTOMER") == 0 && num_tokens == 4 && role_id == 1) {
                int new_uid = -1, new_acc = -1;
                int rc = db_create_user(tokens[2], tokens[3], ROLE_CUSTOMER, 0.0, &new_uid, &new_acc);
                if (rc == 0) snprintf(response, BUFFER_SIZE, "SUCCESS|%d|%d", new_uid, new_acc);
                else if (rc == -2) strcpy(response, "ERROR|Account creation failed (Username likely exists).");
                else strcpy(response, "ERROR|Account creation failed.");
            } else if (strcmp(action, "LOGIN") == 0 && num_tokens == 4) {
                UserRecord u;
                int status = db_authenticate(tokens[2], tokens[3], role_id, &u);
                if (status == 1) {
                    if (role_id == ROLE_CUSTOMER) {
                        AccountRecord a;
                        if (db_get_account(u.user_id, &a)) {
                            snprintf(response, BUFFER_SIZE, "SUCCESS|%d|%d|%d", u.user_id, u.role_id, a.account_id);
                        } else {
                            snprintf(response, BUFFER_SIZE, "SUCCESS|%d|%d", u.user_id, u.role_id);
                        }
                    } else {
                        snprintf(response, BUFFER_SIZE, "SUCCESS|%d|%d", u.user_id, u.role_id);
                    }
                }
                else if (status == -2) strcpy(response, "ERROR|Role mismatch.");
                else if (status == -3) strcpy(response, "ERROR|Account deactivated.");
                else if (status == -4) strcpy(response, "ERROR|ALREADY_LOGGED_IN");
                else strcpy(response, "ERROR|Invalid credentials or user not found.");
            }
            // Customer Operations
            else if (role_id == ROLE_CUSTOMER) {
                if (strcmp(action, "VIEW_BALANCE") == 0) {
                    double bal = 0.0;
                    int acc_id = 0;
                    if (db_get_balance(user_id, &bal, &acc_id)) snprintf(response, BUFFER_SIZE, "SUCCESS|%.2f (Account No: %d)", bal, acc_id);
                    else strcpy(response, "ERROR|Account not found.");
                } else if (strcmp(action, "DEPOSIT") == 0 && num_tokens == 4) {
                    char err[128];
                    if (db_deposit(user_id, atof(tokens[3]), err, sizeof(err)) == 0)
                        strcpy(response, "SUCCESS|Deposit successful.");
                    else snprintf(response, BUFFER_SIZE, "ERROR|%s", err);
                } else if (strcmp(action, "WITHDRAW") == 0 && num_tokens == 4) {
                    char err[128];
                    if (db_withdraw(user_id, atof(tokens[3]), err, sizeof(err)) == 0)
                        strcpy(response, "SUCCESS|Withdrawal successful.");
                    else snprintf(response, BUFFER_SIZE, "ERROR|%s", err);
                } else if (strcmp(action, "TRANSFER") == 0 && num_tokens == 5) {
                    char err[128];
                    if (db_transfer(user_id, atoi(tokens[3]), atof(tokens[4]), err, sizeof(err)) == 0)
                        strcpy(response, "SUCCESS|Transfer successful.");
                    else snprintf(response, BUFFER_SIZE, "ERROR|%s", err);
                } else if (strcmp(action, "VIEW_HISTORY") == 0) {
                    char details[BUFFER_SIZE - 64] = {0};
                    if (db_get_transactions(user_id, 10, details, sizeof(details)) > 0)
                        snprintf(response, BUFFER_SIZE, "SUCCESS|History:\n%s", details);
                    else strcpy(response, "SUCCESS|No transactions found.");
                } else if (strcmp(action, "CHANGE_PASS") == 0 && num_tokens == 4) {
                    if (db_change_password(user_id, tokens[3]) == 0)
                        strcpy(response, "SUCCESS|Password changed successfully.");
                    else strcpy(response, "ERROR|Failed to change password.");
                } else if (strcmp(action, "APPLY_LOAN") == 0 && num_tokens == 4) {
                    char err[128];
                    int new_loan_id = -1;
                    if (db_apply_loan(user_id, atof(tokens[3]), &new_loan_id, err, sizeof(err)) == 0)
                        snprintf(response, BUFFER_SIZE, "SUCCESS|Loan application submitted (Loan ID: %d). Status: PENDING.", new_loan_id);
                    else snprintf(response, BUFFER_SIZE, "ERROR|%s", err);
                } else if (strcmp(action, "ADD_FEEDBACK") == 0 && num_tokens == 4) {
                    if (strlen(tokens[3]) < 5) strcpy(response, "ERROR|Feedback message is too short.");
                    else if (db_add_feedback(user_id, tokens[3]) == 0) strcpy(response, "SUCCESS|Feedback submitted. Thank you.");
                    else strcpy(response, "ERROR|Feedback submission failed.");
                }
            }
            // Bank Employee Operations
            else if (role_id == ROLE_EMPLOYEE) {
                if (strcmp(action, "ADD_CUSTOMER") == 0 && num_tokens == 6) {
                    int new_uid = -1, new_acc = -1;
                    if (db_create_user(tokens[3], tokens[4], ROLE_CUSTOMER, atof(tokens[5]), &new_uid, &new_acc) == 0)
                        snprintf(response, BUFFER_SIZE, "SUCCESS|Customer created. User ID: %d, Account ID: %d", new_uid, new_acc);
                    else strcpy(response, "ERROR|Creation failed.");
                } else if (strcmp(action, "MOD_CUSTOMER") == 0 && num_tokens == 5) {
                    if (db_modify_user_password(atoi(tokens[3]), tokens[4], ROLE_CUSTOMER) == 0)
                        strcpy(response, "SUCCESS|Customer details modified.");
                    else strcpy(response, "ERROR|Failed to modify details.");
                } else if (strcmp(action, "VIEW_PASSBOOK") == 0 && num_tokens == 4) {
                    char details[BUFFER_SIZE - 64] = {0};
                    if (db_get_transactions(atoi(tokens[3]), 20, details, sizeof(details)) > 0)
                        snprintf(response, BUFFER_SIZE, "SUCCESS|Passbook History:\n%s", details);
                    else strcpy(response, "SUCCESS|No transactions found for that user.");
                } else if (strcmp(action, "VIEW_ASSIGNED_LOANS") == 0 && num_tokens == 3) {
                    char details[BUFFER_SIZE - 64] = {0};
                    if (db_view_assigned_loans(user_id, details, sizeof(details)) > 0)
                        snprintf(response, BUFFER_SIZE, "SUCCESS|%s", details);
                    else strcpy(response, "SUCCESS|No loans currently assigned to you.");
                } else if (strcmp(action, "PROCESS_LOAN") == 0 && num_tokens == 5) {
                    char err[128];
                    if (db_process_loan(atoi(tokens[3]), tokens[4], user_id, err, sizeof(err)) == 0)
                        snprintf(response, BUFFER_SIZE, "SUCCESS|Loan %s processed: %s.", tokens[3], tokens[4]);
                    else snprintf(response, BUFFER_SIZE, "ERROR|%s", err);
                } else if (strcmp(action, "CHANGE_PASS") == 0 && num_tokens == 4) {
                    if (db_change_password(user_id, tokens[3]) == 0)
                        strcpy(response, "SUCCESS|Password changed successfully.");
                    else strcpy(response, "ERROR|Failed to change password.");
                }
            }
            // Manager Operations
            else if (role_id == ROLE_MANAGER) {
                if (strcmp(action, "ACT_DEACT_ACC") == 0 && num_tokens == 5) {
                    int status = (strcmp(tokens[4], "ACTIVATE") == 0) ? 1 : 0;
                    int rc = db_activate_deactivate(atoi(tokens[3]), status);
                    if (rc == 0)
                        snprintf(response, BUFFER_SIZE, "SUCCESS|Account %s status set to %s.", tokens[3], tokens[4]);
                    else if (rc == -2)
                        strcpy(response, "ERROR|Only customer accounts can be activated or deactivated.");
                    else strcpy(response, "ERROR|Failed to modify account status (user not found).");
                } else if (strcmp(action, "VIEW_LOANS") == 0 && num_tokens == 3) {
                    char details[BUFFER_SIZE - 64] = {0};
                    if (db_view_pending_loans(details, sizeof(details)) > 0)
                        snprintf(response, BUFFER_SIZE, "SUCCESS|%s", details);
                    else strcpy(response, "SUCCESS|No pending loan applications found.");
                } else if (strcmp(action, "ASSIGN_LOAN") == 0 && num_tokens == 5) {
                    char err[128];
                    if (db_assign_loan(atoi(tokens[3]), atoi(tokens[4]), err, sizeof(err)) == 0)
                        snprintf(response, BUFFER_SIZE, "SUCCESS|Loan %s assigned to Employee %s.", tokens[3], tokens[4]);
                    else snprintf(response, BUFFER_SIZE, "ERROR|%s", err);
                } else if (strcmp(action, "REVIEW_FEEDBACK") == 0 && num_tokens == 3) {
                    char details[BUFFER_SIZE - 64] = {0};
                    if (db_review_feedback(details, sizeof(details)) > 0)
                        snprintf(response, BUFFER_SIZE, "SUCCESS|%s", details);
                    else strcpy(response, "SUCCESS|No feedback found.");
                } else if (strcmp(action, "CHANGE_PASS") == 0 && num_tokens == 4) {
                    if (db_change_password(user_id, tokens[3]) == 0)
                        strcpy(response, "SUCCESS|Password changed successfully.");
                    else strcpy(response, "ERROR|Failed to change password.");
                }
            }
            // Administrator Operations
            else if (role_id == ROLE_ADMIN) {
                if (strcmp(action, "ADD_EMPLOYEE") == 0 && num_tokens == 5) {
                    int new_uid = -1;
                    if (db_create_user(tokens[3], tokens[4], ROLE_EMPLOYEE, 0.0, &new_uid, NULL) == 0)
                        strcpy(response, "SUCCESS|New Bank Employee account created.");
                    else strcpy(response, "ERROR|Creation failed.");
                } else if (strcmp(action, "MOD_USER_DETAILS") == 0 && num_tokens == 5) {
                    if (db_modify_user_password(atoi(tokens[3]), tokens[4], 0) == 0)
                        strcpy(response, "SUCCESS|Details (Password) modified for user.");
                    else strcpy(response, "ERROR|Failed to modify details.");
                } else if (strcmp(action, "MANAGE_ROLES") == 0 && num_tokens == 5) {
                    if (db_manage_user_role(atoi(tokens[3]), atoi(tokens[4])) == 0)
                        strcpy(response, "SUCCESS|User role updated successfully.");
                    else strcpy(response, "ERROR|Failed to update user role.");
                } else if (strcmp(action, "CHANGE_PASS") == 0 && num_tokens == 4) {
                    if (db_change_password(user_id, tokens[3]) == 0)
                        strcpy(response, "SUCCESS|Password changed successfully.");
                    else strcpy(response, "ERROR|Failed to change password.");
                }
            }
        }
    }

send_and_exit:
    send(client_fd, response, strlen(response), 0);
    close(client_fd);
    exit(0);
}

/* Main server */

int main(int argc, char *argv[]) {
    // If run with --init or -init, reset database seed and exit
    if (argc > 1 && (strcmp(argv[1], "--init") == 0 || strcmp(argv[1], "-init") == 0)) {
        printf("Resetting and initializing database files...\n");
        db_init(1);
        return 0;
    }

    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, NULL);

    if (db_init(0) != 0) {
        fprintf(stderr, "Failed to initialize database.\n");
        return 1;
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket creation failed");
        return 1;
    }

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(PORT);
    saddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sockfd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
        perror("bind failed");
        close(sockfd);
        return 1;
    }

    if (listen(sockfd, BACKLOG) < 0) {
        perror("listen failed");
        close(sockfd);
        return 1;
    }

    printf("Bank server running on port %d...\n", PORT);

    while (1) {
        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);
        int client_fd = accept(sockfd, (struct sockaddr *)&caddr, &clen);
        if (client_fd < 0) continue;

        if (fork() == 0) {
            close(sockfd);
            handle_child_process(client_fd);
        } else {
            close(client_fd);
        }
    }

    close(sockfd);
    return 0;
}
