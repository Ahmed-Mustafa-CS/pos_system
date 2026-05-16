# ShopFlow POS — Point-of-Sale & Inventory System

A modern desktop Point-of-Sale and inventory management system built with **C++ and SFML 3**. ShopFlow POS handles everything a small retail business needs — scanning products, processing sales, managing inventory, tracking suppliers, and handling multiple staff accounts — all in a dark-themed UI inspired by Shopify POS and Stripe Dashboard.

---

## Screens

### Login Screen
Staff members log in with a username and password. The system supports two roles — **Admin** and **Cashier** — with different levels of access across the app. Press Enter to submit.

Default accounts:
| Username | Password | Role    |
|----------|----------|---------|
| admin    | admin123 | Admin   |
| cashier  | cash123  | Cashier |

### Dashboard
The home screen after login. Shows a business overview with quick-action buttons to jump straight into making a sale, viewing inventory, or checking transactions.

### POS (Point of Sale)
The main selling screen. Browse products by category using filter pills at the top, search for items by name, and add them to the cart. The cart shows each item, quantity, and price with a running total. Supports scrolling when the cart overflows. Hit checkout when ready.

### Checkout Confirmation
Before finalizing a sale, a confirmation screen shows a full receipt preview — items, quantities, prices, and total. Confirm to complete the transaction or go back to edit the cart.

### Inventory
A full product table showing all items with their category, price, and stock level. Admins can add new products, edit existing ones (including updating stock), and delete products. Cashiers can view but not modify.

### Suppliers
Manage the list of suppliers. Admins can add new suppliers and delete existing ones.

### Transaction History
A log of all completed sales — date, items sold, and total amount. Useful for reviewing past activity.

### User Management *(Admin only)*
View all staff accounts. Admins can add new users (assigning them as admin or cashier) and delete existing accounts.

---

## How to Compile & Run

**Requirements:**
- MSYS2 with UCRT64 environment
- SFML 3 installed via: `pacman -S mingw-w64-ucrt-x86_64-sfml`
- `arial.ttf` in the same folder as the executable

**Compile:**
```bash
C:\msys64\ucrt64\bin\g++ -std=c++17 pos_system_v2.cpp -o pos.exe -I"C:\msys64\ucrt64\include" -L"C:\msys64\ucrt64\lib" -lsfml-graphics -lsfml-window -lsfml-system
```

**Run:**
```bash
./pos.exe
```

---

## Data Files
All data is saved automatically on every change:
- `products.txt` — product catalog
- `transactions.txt` — sales history
- `users.txt` — staff accounts
