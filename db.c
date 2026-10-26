#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "bank.h"


/* Record locking helpers using fcntl */

int lock_record(int fd, int type, off_t offset, off_t len, int wait) {
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type   = type;   // F_RDLCK or F_WRLCK
    fl.l_whence = SEEK_SET;
    fl.l_start  = offset;
    fl.l_len    = len;
    fl.l_pid    = getpid();
    return fcntl(fd, wait ? F_SETLKW : F_SETLK, &fl);
}

int unlock_record(int fd, off_t offset, off_t len) {
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type   = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start  = offset;
    fl.l_len    = len;
    fl.l_pid    = getpid();
    return fcntl(fd, F_SETLK, &fl);
}

void get_current_timestamp(char *buffer, size_t max_len) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (t) {
        strftime(buffer, max_len, "%Y-%m-%d %H:%M:%S", t);
    } else {
        snprintf(buffer, max_len, "1970-01-01 00:00:00");
    }
}

/* Helper functions to find records in binary files */

static int find_user_by_id(int uid, UserRecord *out, off_t *out_off) {
    int fd = open(USERS_FILE, O_RDONLY);
    if (fd < 0) return 0;
    UserRecord u;
    off_t off = 0;
    while (read(fd, &u, sizeof(UserRecord)) == sizeof(UserRecord)) {
        if (u.user_id == uid) {
            if (out) *out = u;
            if (out_off) *out_off = off;
            close(fd);
            return 1;
        }
        off += sizeof(UserRecord);
    }
    close(fd);
    return 0;
}

static int find_user_by_name(const char *name, UserRecord *out, off_t *out_off) {
    int fd = open(USERS_FILE, O_RDONLY);
    if (fd < 0) return 0;
    UserRecord u;
    off_t off = 0;
    while (read(fd, &u, sizeof(UserRecord)) == sizeof(UserRecord)) {
        if (strcmp(u.username, name) == 0) {
            if (out) *out = u;
            if (out_off) *out_off = off;
            close(fd);
            return 1;
        }
        off += sizeof(UserRecord);
    }
    close(fd);
    return 0;
}

static int find_acc_by_user_id(int uid, AccountRecord *out, off_t *out_off) {
    int fd = open(ACCOUNTS_FILE, O_RDONLY);
    if (fd < 0) return 0;
    AccountRecord a;
    off_t off = 0;
    while (read(fd, &a, sizeof(AccountRecord)) == sizeof(AccountRecord)) {
        if (a.user_id == uid) {
            if (out) *out = a;
            if (out_off) *out_off = off;
            close(fd);
            return 1;
        }
        off += sizeof(AccountRecord);
    }
    close(fd);
    return 0;
}

static int find_acc_by_id(int acc_id, AccountRecord *out, off_t *out_off) {
    int fd = open(ACCOUNTS_FILE, O_RDONLY);
    if (fd < 0) return 0;
    AccountRecord a;
    off_t off = 0;
    while (read(fd, &a, sizeof(AccountRecord)) == sizeof(AccountRecord)) {
        if (a.account_id == acc_id) {
            if (out) *out = a;
            if (out_off) *out_off = off;
            close(fd);
            return 1;
        }
        off += sizeof(AccountRecord);
    }
    close(fd);
    return 0;
}

/* Transaction history logging */

static int db_add_transaction(int account_id, const char *type, double amount, const char *details) {
    int fd = open(TRANSACTIONS_FILE, O_RDWR | O_CREAT, 0644);
    if (fd < 0) return -1;
    lock_record(fd, F_WRLCK, 0, 0, 1);

    int max_id = 0;
    TransactionRecord tr;
    while (read(fd, &tr, sizeof(TransactionRecord)) == sizeof(TransactionRecord)) {
        if (tr.txn_id > max_id) max_id = tr.txn_id;
    }

    TransactionRecord new_tr;
    memset(&new_tr, 0, sizeof(new_tr));
    new_tr.txn_id = max_id + 1;
    new_tr.account_id = account_id;
    strncpy(new_tr.type, type, sizeof(new_tr.type) - 1);
    new_tr.amount = amount;
    strncpy(new_tr.details, details, sizeof(new_tr.details) - 1);
    get_current_timestamp(new_tr.timestamp, sizeof(new_tr.timestamp));

    lseek(fd, 0, SEEK_END);
    write(fd, &new_tr, sizeof(TransactionRecord));
    fsync(fd);
    unlock_record(fd, 0, 0);
    close(fd);
    return 0;
}

int db_get_transactions(int user_id, int limit, char *out_buf, size_t max_len) {
    AccountRecord acc;
    if (!find_acc_by_user_id(user_id, &acc, NULL)) return 0;

    int fd = open(TRANSACTIONS_FILE, O_RDONLY);
    if (fd < 0) return 0;
    lock_record(fd, F_RDLCK, 0, 0, 1);

    TransactionRecord txns[100];
    int count = 0;
    TransactionRecord tr;
    while (read(fd, &tr, sizeof(TransactionRecord)) == sizeof(TransactionRecord) && count < 100) {
        if (tr.account_id == acc.account_id) {
            txns[count++] = tr;
        }
    }
    unlock_record(fd, 0, 0);
    close(fd);

    if (count == 0) return 0;
    int start = (count > limit) ? (count - limit) : 0;
    out_buf[0] = '\0';

    for (int i = count - 1; i >= start; i--) {
        char line[256];
        snprintf(line, sizeof(line), "%s|%s|%.2f|%s\n",
                 txns[i].timestamp, txns[i].type, txns[i].amount, txns[i].details);
        if (strlen(out_buf) + strlen(line) < max_len - 1) {
            strcat(out_buf, line);
        }
    }
    return count;
}

/* Database initialization and session cleanup */

void db_reset_login_status(void) {
    int fd = open(USERS_FILE, O_RDWR);
    if (fd < 0) return;
    lock_record(fd, F_WRLCK, 0, 0, 1);

    UserRecord u;
    while (read(fd, &u, sizeof(UserRecord)) == sizeof(UserRecord)) {
        if (u.is_logged_in != 0) {
            u.is_logged_in = 0;
            lseek(fd, -((off_t)sizeof(UserRecord)), SEEK_CUR);
            write(fd, &u, sizeof(UserRecord));
        }
    }
    fsync(fd);
    unlock_record(fd, 0, 0);
    close(fd);
}

int db_init(int force_reset) {
    if (force_reset) {
        unlink(USERS_FILE);
        unlink(ACCOUNTS_FILE);
        unlink(TRANSACTIONS_FILE);
        unlink(LOANS_FILE);
        unlink(FEEDBACK_FILE);
    }

    int fd_users = open(USERS_FILE, O_RDWR | O_CREAT, 0644);
    if (fd_users < 0) return -1;

    struct stat st;
    fstat(fd_users, &st);
    if (st.st_size == 0) {
        UserRecord users[] = {
            {101, "cust1", "pass",  ROLE_CUSTOMER, 1, 0},
            {102, "cust2", "pass2", ROLE_CUSTOMER, 1, 0},
            {201, "emp1",  "pass",  ROLE_EMPLOYEE, 1, 0},
            {301, "mgr1",  "pass",  ROLE_MANAGER,  1, 0},
            {401, "admin1","pass",  ROLE_ADMIN,    1, 0}
        };
        for (int i = 0; i < 5; i++) {
            write(fd_users, &users[i], sizeof(UserRecord));
        }
        fsync(fd_users);

        int fd_acc = open(ACCOUNTS_FILE, O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (fd_acc >= 0) {
            AccountRecord accs[] = {
                {10001, 101, 5000.00, 1},
                {10002, 102, 200.00,  1}
            };
            for (int i = 0; i < 2; i++) {
                write(fd_acc, &accs[i], sizeof(AccountRecord));
            }
            fsync(fd_acc);
            close(fd_acc);
        }

        db_add_transaction(10001, TXN_DEPOSIT, 5000.00, "Initial Deposit");
        db_add_transaction(10002, TXN_DEPOSIT, 200.00,  "Initial Deposit");

        int fd_touch;
        if ((fd_touch = open(LOANS_FILE, O_RDWR | O_CREAT, 0644)) >= 0) close(fd_touch);
        if ((fd_touch = open(FEEDBACK_FILE, O_RDWR | O_CREAT, 0644)) >= 0) close(fd_touch);
        if ((fd_touch = open(TRANSACTIONS_FILE, O_RDWR | O_CREAT, 0644)) >= 0) close(fd_touch);
    }
    close(fd_users);
    db_reset_login_status();
    return 0;
}

/* Authentication and user management */

int db_authenticate(const char *name, const char *pass, int role, UserRecord *out_user) {
    int fd = open(USERS_FILE, O_RDWR);
    if (fd < 0) return 0;

    UserRecord u;
    off_t off = 0;
    int found = 0;
    while (read(fd, &u, sizeof(UserRecord)) == sizeof(UserRecord)) {
        if (strcmp(u.username, name) == 0) {
            found = 1;
            break;
        }
        off += sizeof(UserRecord);
    }

    if (!found || strcmp(u.password, pass) != 0) {
        close(fd);
        return 0; // Invalid credentials
    }
    if (u.role_id != role) {
        close(fd);
        return -2; // Role mismatch
    }
    if (u.is_active == 0) {
        close(fd);
        return -3; // Account deactivated
    }

    /* Lock user record to check and set login state */
    lock_record(fd, F_WRLCK, off, sizeof(UserRecord), 1);
    lseek(fd, off, SEEK_SET);
    read(fd, &u, sizeof(UserRecord));

    if (u.is_logged_in == 1) {
        unlock_record(fd, off, sizeof(UserRecord));
        close(fd);
        return -4; // ALREADY_LOGGED_IN
    }

    u.is_logged_in = 1;
    lseek(fd, off, SEEK_SET);
    write(fd, &u, sizeof(UserRecord));
    fsync(fd);
    unlock_record(fd, off, sizeof(UserRecord));
    close(fd);

    if (out_user) *out_user = u;
    return 1;
}

void db_logout(int uid) {
    UserRecord u;
    off_t off;
    if (!find_user_by_id(uid, &u, &off)) return;

    int fd = open(USERS_FILE, O_RDWR);
    if (fd < 0) return;
    lock_record(fd, F_WRLCK, off, sizeof(UserRecord), 1);
    lseek(fd, off, SEEK_SET);
    read(fd, &u, sizeof(UserRecord));
    u.is_logged_in = 0;
    lseek(fd, off, SEEK_SET);
    write(fd, &u, sizeof(UserRecord));
    fsync(fd);
    unlock_record(fd, off, sizeof(UserRecord));
    close(fd);
}

int db_check_session(int uid) {
    UserRecord u;
    if (find_user_by_id(uid, &u, NULL)) {
        return (u.is_logged_in == 1);
    }
    return 0;
}

int db_create_user(const char *name, const char *pass, int role, double init_bal, int *new_id, int *new_acc_id) {
    if (find_user_by_name(name, NULL, NULL)) return -2; // Username taken

    int fd = open(USERS_FILE, O_RDWR | O_CREAT, 0644);
    if (fd < 0) return -1;
    lock_record(fd, F_WRLCK, 0, 0, 1);

    int max_id = (role == ROLE_CUSTOMER) ? 100 : (role * 100);
    UserRecord u;
    while (read(fd, &u, sizeof(UserRecord)) == sizeof(UserRecord)) {
        if (u.role_id == role && u.user_id > max_id) max_id = u.user_id;
    }

    UserRecord new_user;
    memset(&new_user, 0, sizeof(new_user));
    new_user.user_id = max_id + 1;
    strncpy(new_user.username, name, sizeof(new_user.username) - 1);
    strncpy(new_user.password, pass, sizeof(new_user.password) - 1);
    new_user.role_id = role;
    new_user.is_active = 1;
    new_user.is_logged_in = 0;

    lseek(fd, 0, SEEK_END);
    write(fd, &new_user, sizeof(UserRecord));
    fsync(fd);
    unlock_record(fd, 0, 0);
    close(fd);

    if (role == ROLE_CUSTOMER) {
        int fd_acc = open(ACCOUNTS_FILE, O_RDWR | O_CREAT, 0644);
        if (fd_acc >= 0) {
            lock_record(fd_acc, F_WRLCK, 0, 0, 1);
            int max_acc = 10000;
            AccountRecord a;
            while (read(fd_acc, &a, sizeof(AccountRecord)) == sizeof(AccountRecord)) {
                if (a.account_id > max_acc) max_acc = a.account_id;
            }
            AccountRecord new_acc;
            memset(&new_acc, 0, sizeof(new_acc));
            new_acc.account_id = max_acc + 1;
            new_acc.user_id = new_user.user_id;
            new_acc.balance = (init_bal >= 0) ? init_bal : 0.0;
            new_acc.is_active = 1;

            lseek(fd_acc, 0, SEEK_END);
            write(fd_acc, &new_acc, sizeof(AccountRecord));
            fsync(fd_acc);
            unlock_record(fd_acc, 0, 0);
            close(fd_acc);

            if (new_acc_id) *new_acc_id = new_acc.account_id;

            if (new_acc.balance > 0) {
                db_add_transaction(new_acc.account_id, TXN_DEPOSIT, new_acc.balance, "Initial Deposit");
            }
        }
    } else {
        if (new_acc_id) *new_acc_id = -1;
    }
    if (new_id) *new_id = new_user.user_id;
    return 0;
}

int db_change_password(int uid, const char *new_pass) {
    UserRecord u;
    off_t off;
    if (!find_user_by_id(uid, &u, &off)) return -1;
    int fd = open(USERS_FILE, O_RDWR);
    if (fd < 0) return -1;
    lock_record(fd, F_WRLCK, off, sizeof(UserRecord), 1);
    lseek(fd, off, SEEK_SET);
    read(fd, &u, sizeof(UserRecord));
    strncpy(u.password, new_pass, sizeof(u.password) - 1);
    lseek(fd, off, SEEK_SET);
    write(fd, &u, sizeof(UserRecord));
    fsync(fd);
    unlock_record(fd, off, sizeof(UserRecord));
    close(fd);
    return 0;
}

int db_modify_user_password(int target_uid, const char *new_pass, int role_filter) {
    UserRecord u;
    off_t off;
    if (!find_user_by_id(target_uid, &u, &off)) return -1;
    if (role_filter != 0 && u.role_id != role_filter) return -1;
    return db_change_password(target_uid, new_pass);
}

int db_activate_deactivate(int target_uid, int new_status) {
    UserRecord u;
    off_t off;
    if (!find_user_by_id(target_uid, &u, &off)) return -1;
    if (u.role_id != ROLE_CUSTOMER) return -2;

    int fd = open(USERS_FILE, O_RDWR);
    if (fd < 0) return -1;
    lock_record(fd, F_WRLCK, off, sizeof(UserRecord), 1);
    lseek(fd, off, SEEK_SET);
    read(fd, &u, sizeof(UserRecord));
    u.is_active = new_status;
    if (new_status == 0) u.is_logged_in = 0;
    lseek(fd, off, SEEK_SET);
    write(fd, &u, sizeof(UserRecord));
    fsync(fd);
    unlock_record(fd, off, sizeof(UserRecord));
    close(fd);

    AccountRecord a;
    off_t a_off;
    if (find_acc_by_user_id(target_uid, &a, &a_off)) {
        int fd_a = open(ACCOUNTS_FILE, O_RDWR);
        if (fd_a >= 0) {
            lock_record(fd_a, F_WRLCK, a_off, sizeof(AccountRecord), 1);
            lseek(fd_a, a_off, SEEK_SET);
            read(fd_a, &a, sizeof(AccountRecord));
            a.is_active = new_status;
            lseek(fd_a, a_off, SEEK_SET);
            write(fd_a, &a, sizeof(AccountRecord));
            fsync(fd_a);
            unlock_record(fd_a, a_off, sizeof(AccountRecord));
            close(fd_a);
        }
    }
    return 0;
}

int db_manage_user_role(int target_uid, int new_role) {
    if (new_role < ROLE_CUSTOMER || new_role > ROLE_ADMIN) return -1;
    UserRecord u;
    off_t off;
    if (!find_user_by_id(target_uid, &u, &off)) return -1;
    int fd = open(USERS_FILE, O_RDWR);
    if (fd < 0) return -1;
    lock_record(fd, F_WRLCK, off, sizeof(UserRecord), 1);
    lseek(fd, off, SEEK_SET);
    read(fd, &u, sizeof(UserRecord));
    u.role_id = new_role;
    lseek(fd, off, SEEK_SET);
    write(fd, &u, sizeof(UserRecord));
    fsync(fd);
    unlock_record(fd, off, sizeof(UserRecord));
    close(fd);
    return 0;
}

/* Balance and transfer operations */

int db_get_account(int uid, AccountRecord *out) {
    return find_acc_by_user_id(uid, out, NULL);
}

int db_get_balance(int uid, double *out_bal, int *out_acc_id) {
    AccountRecord a;
    if (find_acc_by_user_id(uid, &a, NULL)) {
        if (out_bal) *out_bal = a.balance;
        if (out_acc_id) *out_acc_id = a.account_id;
        return 1;
    }
    return 0;
}

int db_deposit(int uid, double amount, char *err, size_t err_len) {
    if (amount <= 0) {
        snprintf(err, err_len, "Invalid deposit amount.");
        return -1;
    }
    AccountRecord a;
    off_t off;
    if (!find_acc_by_user_id(uid, &a, &off)) {
        snprintf(err, err_len, "Account not found.");
        return -1;
    }
    int fd = open(ACCOUNTS_FILE, O_RDWR);
    if (fd < 0) return -1;

    lock_record(fd, F_WRLCK, off, sizeof(AccountRecord), 1);
    lseek(fd, off, SEEK_SET);
    read(fd, &a, sizeof(AccountRecord));
    if (a.is_active == 0) {
        unlock_record(fd, off, sizeof(AccountRecord));
        close(fd);
        snprintf(err, err_len, "Account is deactivated.");
        return -1;
    }
    a.balance += amount;
    lseek(fd, off, SEEK_SET);
    write(fd, &a, sizeof(AccountRecord));
    fsync(fd);
    unlock_record(fd, off, sizeof(AccountRecord));
    close(fd);

    db_add_transaction(a.account_id, TXN_DEPOSIT, amount, "Self Deposit");
    return 0;
}

int db_withdraw(int uid, double amount, char *err, size_t err_len) {
    if (amount <= 0) {
        snprintf(err, err_len, "Invalid withdrawal amount.");
        return -1;
    }
    AccountRecord a;
    off_t off;
    if (!find_acc_by_user_id(uid, &a, &off)) {
        snprintf(err, err_len, "Account not found.");
        return -1;
    }
    int fd = open(ACCOUNTS_FILE, O_RDWR);
    if (fd < 0) return -1;

    lock_record(fd, F_WRLCK, off, sizeof(AccountRecord), 1);
    lseek(fd, off, SEEK_SET);
    read(fd, &a, sizeof(AccountRecord));
    if (a.is_active == 0) {
        unlock_record(fd, off, sizeof(AccountRecord));
        close(fd);
        snprintf(err, err_len, "Account is deactivated.");
        return -1;
    }
    if (a.balance < amount) {
        unlock_record(fd, off, sizeof(AccountRecord));
        close(fd);
        snprintf(err, err_len, "Insufficient funds.");
        return -1;
    }
    a.balance -= amount;
    lseek(fd, off, SEEK_SET);
    write(fd, &a, sizeof(AccountRecord));
    fsync(fd);
    unlock_record(fd, off, sizeof(AccountRecord));
    close(fd);

    db_add_transaction(a.account_id, TXN_WITHDRAW, amount, "Self Withdrawal");
    return 0;
}

/* Transfer money between two accounts */
int db_transfer(int sender_uid, int target_acc_id, double amount, char *err, size_t err_len) {
    if (amount <= 0) {
        snprintf(err, err_len, "Invalid transfer amount.");
        return -1;
    }
    AccountRecord sender, target;
    off_t s_off, t_off;
    if (!find_acc_by_user_id(sender_uid, &sender, &s_off)) {
        snprintf(err, err_len, "Sender account not found.");
        return -1;
    }
    if (sender.account_id == target_acc_id) {
        snprintf(err, err_len, "Cannot transfer to the same account.");
        return -1;
    }
    if (!find_acc_by_id(target_acc_id, &target, &t_off)) {
        snprintf(err, err_len, "Target account not found.");
        return -1;
    }

    int fd = open(ACCOUNTS_FILE, O_RDWR);
    if (fd < 0) return -1;

    /* Always lock lower offset first to prevent deadlocks */
    off_t first_off  = (s_off < t_off) ? s_off : t_off;
    off_t second_off = (s_off < t_off) ? t_off : s_off;
    lock_record(fd, F_WRLCK, first_off, sizeof(AccountRecord), 1);
    lock_record(fd, F_WRLCK, second_off, sizeof(AccountRecord), 1);

    lseek(fd, s_off, SEEK_SET);
    read(fd, &sender, sizeof(AccountRecord));
    lseek(fd, t_off, SEEK_SET);
    read(fd, &target, sizeof(AccountRecord));

    if (sender.is_active == 0 || target.is_active == 0) {
        unlock_record(fd, second_off, sizeof(AccountRecord));
        unlock_record(fd, first_off, sizeof(AccountRecord));
        close(fd);
        snprintf(err, err_len, "One of the accounts is deactivated.");
        return -1;
    }
    if (sender.balance < amount) {
        unlock_record(fd, second_off, sizeof(AccountRecord));
        unlock_record(fd, first_off, sizeof(AccountRecord));
        close(fd);
        snprintf(err, err_len, "Insufficient funds.");
        return -1;
    }

    sender.balance -= amount;
    target.balance += amount;

    lseek(fd, s_off, SEEK_SET);
    write(fd, &sender, sizeof(AccountRecord));
    lseek(fd, t_off, SEEK_SET);
    write(fd, &target, sizeof(AccountRecord));
    fsync(fd);

    unlock_record(fd, second_off, sizeof(AccountRecord));
    unlock_record(fd, first_off, sizeof(AccountRecord));
    close(fd);

    char out_dt[128], in_dt[128];
    snprintf(out_dt, sizeof(out_dt), "Transfer OUT to Acc %d", target_acc_id);
    snprintf(in_dt,  sizeof(in_dt),  "Transfer IN from User %d", sender_uid);
    db_add_transaction(sender.account_id, TXN_TRANSFER, amount, out_dt);
    db_add_transaction(target.account_id, TXN_TRANSFER, amount, in_dt);
    return 0;
}

/* Loan applications and customer feedback */

int db_apply_loan(int uid, double amount, int *out_loan_id, char *err, size_t err_len) {
    if (amount <= 0) {
        snprintf(err, err_len, "Invalid loan amount.");
        return -1;
    }
    AccountRecord a;
    if (!find_acc_by_user_id(uid, &a, NULL)) {
        snprintf(err, err_len, "Account not found.");
        return -1;
    }
    if (a.is_active == 0) {
        snprintf(err, err_len, "Account is deactivated.");
        return -1;
    }
    int fd = open(LOANS_FILE, O_RDWR | O_CREAT, 0644);
    if (fd < 0) return -1;
    lock_record(fd, F_WRLCK, 0, 0, 1);

    int max_id = 0;
    LoanRecord lr;
    while (read(fd, &lr, sizeof(LoanRecord)) == sizeof(LoanRecord)) {
        if (lr.loan_id > max_id) max_id = lr.loan_id;
    }

    LoanRecord nl;
    memset(&nl, 0, sizeof(nl));
    nl.loan_id = max_id + 1;
    nl.account_id = a.account_id;
    nl.amount = amount;
    strncpy(nl.status, STATUS_PENDING, sizeof(nl.status) - 1);
    nl.assigned_employee_id = -1;

    lseek(fd, 0, SEEK_END);
    write(fd, &nl, sizeof(LoanRecord));
    fsync(fd);
    unlock_record(fd, 0, 0);
    close(fd);

    if (out_loan_id) *out_loan_id = nl.loan_id;
    return 0;
}

int db_view_pending_loans(char *out, size_t max_len) {
    int fd = open(LOANS_FILE, O_RDONLY);
    if (fd < 0) return 0;
    lock_record(fd, F_RDLCK, 0, 0, 1);

    LoanRecord lr;
    int count = 0;
    out[0] = '\0';
    strncat(out, "Pending Loan Applications:\n", max_len - 1);
    while (read(fd, &lr, sizeof(LoanRecord)) == sizeof(LoanRecord)) {
        if (strcmp(lr.status, STATUS_PENDING) == 0 || strcmp(lr.status, STATUS_ASSIGNED) == 0) {
            char entry[256];
            snprintf(entry, sizeof(entry), "[ID:%d] Acc:%d - $%.2f - Status:%s\n",
                     lr.loan_id, lr.account_id, lr.amount, lr.status);
            if (strlen(out) + strlen(entry) < max_len - 1) {
                strcat(out, entry);
                count++;
            }
        }
    }
    unlock_record(fd, 0, 0);
    close(fd);
    return count;
}

int db_assign_loan(int loan_id, int emp_id, char *err, size_t err_len) {
    UserRecord emp;
    if (!find_user_by_id(emp_id, &emp, NULL) || emp.role_id != ROLE_EMPLOYEE) {
        snprintf(err, err_len, "User %d is not a bank employee.", emp_id);
        return -1;
    }
    if (emp.is_active == 0) {
        snprintf(err, err_len, "Employee %d account is deactivated.", emp_id);
        return -1;
    }

    int fd = open(LOANS_FILE, O_RDWR);
    if (fd < 0) return -1;
    LoanRecord lr;
    off_t off = 0;
    int success = -1;
    while (read(fd, &lr, sizeof(LoanRecord)) == sizeof(LoanRecord)) {
        if (lr.loan_id == loan_id && strcmp(lr.status, STATUS_PENDING) == 0) {
            lock_record(fd, F_WRLCK, off, sizeof(LoanRecord), 1);
            lseek(fd, off, SEEK_SET);
            read(fd, &lr, sizeof(LoanRecord));
            lr.assigned_employee_id = emp_id;
            strncpy(lr.status, STATUS_ASSIGNED, sizeof(lr.status) - 1);
            lseek(fd, off, SEEK_SET);
            write(fd, &lr, sizeof(LoanRecord));
            fsync(fd);
            unlock_record(fd, off, sizeof(LoanRecord));
            success = 0;
            break;
        }
        off += sizeof(LoanRecord);
    }
    close(fd);
    if (success != 0) snprintf(err, err_len, "Loan not found or not in PENDING status.");
    return success;
}

int db_view_assigned_loans(int emp_id, char *out, size_t max_len) {
    int fd = open(LOANS_FILE, O_RDONLY);
    if (fd < 0) return 0;
    lock_record(fd, F_RDLCK, 0, 0, 1);

    LoanRecord lr;
    int count = 0;
    out[0] = '\0';
    strncat(out, "Loans Assigned to You:\n", max_len - 1);
    while (read(fd, &lr, sizeof(LoanRecord)) == sizeof(LoanRecord)) {
        if (lr.assigned_employee_id == emp_id &&
            (strcmp(lr.status, STATUS_ASSIGNED) == 0 || strcmp(lr.status, "PROCESSING") == 0)) {
            char entry[256];
            snprintf(entry, sizeof(entry), "[ID:%d] Acc:%d - $%.2f - Status:%s\n",
                     lr.loan_id, lr.account_id, lr.amount, lr.status);
            if (strlen(out) + strlen(entry) < max_len - 1) {
                strcat(out, entry);
                count++;
            }
        }
    }
    unlock_record(fd, 0, 0);
    close(fd);
    return count;
}

int db_process_loan(int loan_id, const char *status_str, int emp_id, char *err, size_t err_len) {
    (void)emp_id;
    if (strcmp(status_str, STATUS_APPROVED) != 0 && strcmp(status_str, STATUS_REJECTED) != 0) {
        snprintf(err, err_len, "Invalid status. Use APPROVED or REJECTED.");
        return -1;
    }
    int fd = open(LOANS_FILE, O_RDWR);
    if (fd < 0) return -1;
    LoanRecord lr;
    off_t off = 0;
    int found = 0;
    while (read(fd, &lr, sizeof(LoanRecord)) == sizeof(LoanRecord)) {
        if (lr.loan_id == loan_id) {
            found = 1;
            break;
        }
        off += sizeof(LoanRecord);
    }
    if (!found) {
        close(fd);
        snprintf(err, err_len, "Loan ID not found.");
        return -1;
    }

    lock_record(fd, F_WRLCK, off, sizeof(LoanRecord), 1);
    lseek(fd, off, SEEK_SET);
    read(fd, &lr, sizeof(LoanRecord));

    if (strcmp(lr.status, STATUS_APPROVED) == 0) {
        unlock_record(fd, off, sizeof(LoanRecord));
        close(fd);
        snprintf(err, err_len, "Loan is already approved.");
        return -1;
    }

    if (lr.assigned_employee_id != emp_id) {
        unlock_record(fd, off, sizeof(LoanRecord));
        close(fd);
        snprintf(err, err_len, "Loan %d is not assigned to you.", loan_id);
        return -1;
    }

    if (strcmp(status_str, STATUS_APPROVED) == 0) {
        AccountRecord a;
        off_t a_off;
        if (find_acc_by_id(lr.account_id, &a, &a_off)) {
            int fd_a = open(ACCOUNTS_FILE, O_RDWR);
            if (fd_a >= 0) {
                lock_record(fd_a, F_WRLCK, a_off, sizeof(AccountRecord), 1);
                lseek(fd_a, a_off, SEEK_SET);
                read(fd_a, &a, sizeof(AccountRecord));
                a.balance += lr.amount;
                lseek(fd_a, a_off, SEEK_SET);
                write(fd_a, &a, sizeof(AccountRecord));
                fsync(fd_a);
                unlock_record(fd_a, a_off, sizeof(AccountRecord));
                close(fd_a);
                db_add_transaction(lr.account_id, TXN_LOAN_CREDIT, lr.amount, "Loan Approval");
            }
        }
    }

    strncpy(lr.status, status_str, sizeof(lr.status) - 1);
    lseek(fd, off, SEEK_SET);
    write(fd, &lr, sizeof(LoanRecord));
    fsync(fd);
    unlock_record(fd, off, sizeof(LoanRecord));
    close(fd);
    return 0;
}

int db_add_feedback(int uid, const char *msg) {
    int fd = open(FEEDBACK_FILE, O_RDWR | O_CREAT, 0644);
    if (fd < 0) return -1;
    lock_record(fd, F_WRLCK, 0, 0, 1);

    int max_id = 0;
    FeedbackRecord fb;
    while (read(fd, &fb, sizeof(FeedbackRecord)) == sizeof(FeedbackRecord)) {
        if (fb.feedback_id > max_id) max_id = fb.feedback_id;
    }

    FeedbackRecord nfb;
    memset(&nfb, 0, sizeof(nfb));
    nfb.feedback_id = max_id + 1;
    nfb.user_id = uid;
    strncpy(nfb.message, msg, sizeof(nfb.message) - 1);
    get_current_timestamp(nfb.timestamp, sizeof(nfb.timestamp));

    lseek(fd, 0, SEEK_END);
    write(fd, &nfb, sizeof(FeedbackRecord));
    fsync(fd);
    unlock_record(fd, 0, 0);
    close(fd);
    return 0;
}

int db_review_feedback(char *out, size_t max_len) {
    int fd = open(FEEDBACK_FILE, O_RDONLY);
    if (fd < 0) return 0;
    FeedbackRecord fb;
    int count = 0;
    out[0] = '\0';
    strncat(out, "Customer Feedback:\n", max_len - 1);
    while (read(fd, &fb, sizeof(FeedbackRecord)) == sizeof(FeedbackRecord)) {
        char entry[512];
        snprintf(entry, sizeof(entry), "[ID:%d, User:%d] %s (Time: %s)\n",
                 fb.feedback_id, fb.user_id, fb.message, fb.timestamp);
        if (strlen(out) + strlen(entry) < max_len - 1) {
            strcat(out, entry);
            count++;
        }
    }
    close(fd);
    return count;
}
