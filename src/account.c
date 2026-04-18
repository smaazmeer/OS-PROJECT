#include "account.h"
#include "stock.h"
#include <string.h>
#include <stddef.h>
#include <stdio.h>

void init_account(Account *account, int id, double initial_balance) {
    account->id = id;
    account->balance = initial_balance;
    account->num_holdings = 0;
    pthread_mutex_init(&account->account_mutex, NULL);
}

void add_holding(Account *account, const char *stock_name, int quantity, double price) {
    pthread_mutex_lock(&account->account_mutex);
    for (int i = 0; i < account->num_holdings; i++) {
        if (strcmp(account->holdings[i].stock_name, stock_name) == 0) {
            if (quantity > 0) {
                // Buy: update average cost
                double total_cost = account->holdings[i].quantity * account->holdings[i].avg_cost + quantity * price;
                account->holdings[i].quantity += quantity;
                account->holdings[i].avg_cost = total_cost / account->holdings[i].quantity;
            } else {
                // Sell: reduce quantity
                account->holdings[i].quantity += quantity; // quantity is negative
                if (account->holdings[i].quantity <= 0) {
                    // remove
                    for (int j = i; j < account->num_holdings - 1; j++) {
                        account->holdings[j] = account->holdings[j + 1];
                    }
                    account->num_holdings--;
                }
            }
            pthread_mutex_unlock(&account->account_mutex);
            return;
        }
    }
    if (quantity > 0 && account->num_holdings < MAX_HOLDINGS) {
        strcpy(account->holdings[account->num_holdings].stock_name, stock_name);
        account->holdings[account->num_holdings].quantity = quantity;
        account->holdings[account->num_holdings].avg_cost = price;
        account->num_holdings++;
    }
    pthread_mutex_unlock(&account->account_mutex);
}

int get_holding(Account *account, const char *stock_name) {
    pthread_mutex_lock(&account->account_mutex);
    for (int i = 0; i < account->num_holdings; i++) {
        if (strcmp(account->holdings[i].stock_name, stock_name) == 0) {
            int qty = account->holdings[i].quantity;
            pthread_mutex_unlock(&account->account_mutex);
            return qty;
        }
    }
    pthread_mutex_unlock(&account->account_mutex);
    return 0;
}

void update_balance(Account *account, double amount) {
    pthread_mutex_lock(&account->account_mutex);
    account->balance += amount;
    pthread_mutex_unlock(&account->account_mutex);
}

double get_balance(Account *account) {
    double bal;
    pthread_mutex_lock(&account->account_mutex);
    bal = account->balance;
    pthread_mutex_unlock(&account->account_mutex);
    return bal;
}
void save_account_to_file(const char *filename, Account *account) {
    FILE *file = fopen(filename, "wb");
    if (!file) {
        return;
    }

    size_t account_data_size = offsetof(Account, account_mutex);
    pthread_mutex_lock(&account->account_mutex);
    fwrite(account, account_data_size, 1, file);
    pthread_mutex_unlock(&account->account_mutex);

    fclose(file);
}

void load_account_from_file(const char *filename, Account *account) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        return;
    }

    size_t account_data_size = offsetof(Account, account_mutex);
    if (fread(account, account_data_size, 1, file) != 1) {
        fclose(file);
        return;
    }

    fclose(file);
    pthread_mutex_init(&account->account_mutex, NULL);
}
double get_portfolio_value(Account *account, Stock stocks[], int num_stocks) {
    double value = get_balance(account);
    pthread_mutex_lock(&account->account_mutex);
    for (int i = 0; i < account->num_holdings; i++) {
        for (int j = 0; j < num_stocks; j++) {
            if (strcmp(account->holdings[i].stock_name, stocks[j].name) == 0) {
                value += account->holdings[i].quantity * get_price(&stocks[j]);
                break;
            }
        }
    }
    pthread_mutex_unlock(&account->account_mutex);
    return value;
}

double get_total_pl(Account *account, Stock stocks[], int num_stocks) {
    double pl = 0.0;
    pthread_mutex_lock(&account->account_mutex);
    for (int i = 0; i < account->num_holdings; i++) {
        for (int j = 0; j < num_stocks; j++) {
            if (strcmp(account->holdings[i].stock_name, stocks[j].name) == 0) {
                double current_price = get_price(&stocks[j]);
                pl += (current_price - account->holdings[i].avg_cost) * account->holdings[i].quantity;
                break;
            }
        }
    }
    pthread_mutex_unlock(&account->account_mutex);
    return pl;
}