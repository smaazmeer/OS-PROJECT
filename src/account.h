#ifndef ACCOUNT_H
#define ACCOUNT_H

#include "stock.h"
#include <pthread.h>

#define MAX_HOLDINGS 10
#define ACCOUNT_FILENAME "account.dat"

typedef struct {
    char stock_name[50];
    int quantity;
    double avg_cost; // average cost per share
} Holding;

typedef struct {
    int id;
    double balance;
    Holding holdings[MAX_HOLDINGS];
    int num_holdings;
    pthread_mutex_t account_mutex;
} Account;

void init_account(Account *account, int id, double initial_balance);
void add_holding(Account *account, const char *stock_name, int quantity, double price);
void add_holding_locked(Account *account, const char *stock_name, int quantity, double price);
int get_holding(Account *account, const char *stock_name);
int get_holding_locked(Account *account, const char *stock_name);
void update_balance(Account *account, double amount);
double get_balance(Account *account);
double get_portfolio_value(Account *account, Stock stocks[], int num_stocks);
double get_total_pl(Account *account, Stock stocks[], int num_stocks);

void save_account_to_file(const char *filename, Account *account);
void load_account_from_file(const char *filename, Account *account);

#endif