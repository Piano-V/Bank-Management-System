#ifndef BANK_H
#define BANK_H

#include <time.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>

#define USERS_FILE        "users.dat"
#define ACCOUNTS_FILE     "accounts.dat"
#define TRANSACTIONS_FILE "transactions.dat"
#define LOANS_FILE        "loans.dat"
#define FEEDBACK_FILE     "feedback.dat"

#define PORT        8080
#define BUFFER_SIZE 2048

/* User roles */
#define ROLE_CUSTOMER 1
#define ROLE_EMPLOYEE 2
#define ROLE_MANAGER  3
#define ROLE_ADMIN    4

/* Loan status values */
#define STATUS_PENDING  "PENDING"
#define STATUS_ASSIGNED "ASSIGNED"
#define STATUS_APPROVED "APPROVED"
#define STATUS_REJECTED "REJECTED"

/* Transaction types */
#define TXN_DEPOSIT     "DEPOSIT"
#define TXN_WITHDRAW    "WITHDRAW"
#define TXN_TRANSFER    "TRANSFER"
#define TXN_LOAN_CREDIT "LOAN_CREDIT"

typedef struct {
    int user_id;
    char username[50];
    char password[50];
    int role_id;
    int is_active;     /* 1=active, 0=deactivated */
    int is_logged_in;  /* 1=logged in, 0=logged out */
} UserRecord;

typedef struct {
    int account_id;
    int user_id;
    double balance;
    int is_active;
} AccountRecord;

typedef struct {
    int txn_id;
    int account_id;
    char type[20];
    double amount;
    char details[128];
    char timestamp[32];
} TransactionRecord;

typedef struct {
    int loan_id;
    int account_id;
    double amount;
    char status[20];
    int assigned_employee_id;
} LoanRecord;

typedef struct {
    int feedback_id;
    int user_id;
    char message[256];
    char timestamp[32];
} FeedbackRecord;

void get_current_timestamp(char *buffer, size_t max_len);

/* Record locking */
int lock_record(int fd, int type, off_t offset, off_t len, int wait);
int unlock_record(int fd, off_t offset, off_t len);

/* Database operations */
int db_init(int force_reset);
void db_reset_login_status(void);

int db_authenticate(const char *name, const char *pass, int role, UserRecord *out_user);
void db_logout(int uid);
int db_check_session(int uid);
int db_create_user(const char *name, const char *pass, int role, double init_bal, int *new_id, int *new_acc_id);
int db_change_password(int uid, const char *new_pass);
int db_modify_user_password(int target_uid, const char *new_pass, int role_filter);
int db_activate_deactivate(int target_uid, int new_status);
int db_manage_user_role(int target_uid, int new_role);

int db_get_account(int uid, AccountRecord *out);
int db_get_balance(int uid, double *out_bal, int *out_acc_id);
int db_deposit(int uid, double amount, char *err, size_t err_len);
int db_withdraw(int uid, double amount, char *err, size_t err_len);
int db_transfer(int sender_uid, int target_acc_id, double amount, char *err, size_t err_len);
int db_get_transactions(int user_id, int limit, char *out_buf, size_t max_len);

int db_apply_loan(int uid, double amount, int *out_loan_id, char *err, size_t err_len);
int db_view_pending_loans(char *out, size_t max_len);
int db_assign_loan(int loan_id, int emp_id, char *err, size_t err_len);
int db_view_assigned_loans(int emp_id, char *out, size_t max_len);
int db_process_loan(int loan_id, const char *status_str, int emp_id, char *err, size_t err_len);
int db_add_feedback(int uid, const char *msg);
int db_review_feedback(char *out, size_t max_len);

#endif
