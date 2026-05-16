/*
=============================================================
   ShopFlow POS  —  Modern Inventory & Point-of-Sale System
   Built with SFML 3  |  Evolved from CliniDo architecture
   v2: Complete system — all screens, bug fixes, visual polish

   Architecture inherited from CliniDo (clinic_gui_v3.cpp):
     • Screen enum  + switch dispatch
     • vector<Button>  per frame (same pattern)
     • InputField  + focusedField pointer
     • drawRect / drawText / drawButton primitives
     • loadData / saveData  file persistence
     • setStatus() toast bar at bottom

   Visual style: Shopify POS / Stripe Dashboard / Lightspeed
     • Dark navy background  (#0F141E)
     • Cyan-teal accent       (#00D4AA)
     • Card surfaces          (#1C2638 / #222E44)
     • Green success, Amber warning, Red danger

   NEW in v2 vs v1:
     • Checkout confirmation screen  (SCREEN_CHECKOUT_CONFIRM)
     • User management screen        (SCREEN_USERS)
     • Add user screen               (SCREEN_ADD_USER)
     • Delete product from edit screen
     • Delete supplier
     • Fixed category-pill label parsing  (no trailing _ issue)
     • Fixed button draw order in POS (overlapping fix)
     • Scrollable cart (overflow guard)
     • Dashboard quick-action buttons
     • Improved table column widths
     • Drawer divider lines in sidebar
     • Animated search-placeholder hint
     • Receipt preview on checkout confirm screen
     • Admin-only guard on protected screens
     • Keyboard Enter to submit login

HOW TO COMPILE (MSYS2 / ucrt64):
  g++ pos_system_v2.cpp -o pos.exe ^
    -I"C:\msys64\ucrt64\include" ^
    -L"C:\msys64\ucrt64\lib" ^
    -lsfml-graphics -lsfml-window -lsfml-system

REQUIRED FILES in same folder:
  - arial.ttf          (any standard Arial / sans-serif TTF)

DATA FILES (auto-created on first run):
  - products.txt
  - transactions.txt
  - users.txt
  - suppliers.txt
  - receipt_<TXID>.txt  (one file per checkout)
=============================================================
*/

#include <SFML/Graphics.hpp>
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <ctime>
#include <cmath>
#include <iomanip>
using namespace std;

// ============================================================
// ======================== PALETTE ===========================
// ============================================================
// All colours are global constants — same pattern as CliniDo.
// Shopify/Stripe dark-dashboard palette.
const sf::Color C_BG      (15,  20,  30);   // page background
const sf::Color C_SIDEBAR (18,  25,  40);   // left sidebar
const sf::Color C_NAV     (10,  14,  22);   // top navbar strip
const sf::Color C_CARD    (26,  36,  54);   // card / panel surface
const sf::Color C_CARD2   (32,  44,  64);   // alt row / lighter card
const sf::Color C_CARD3   (38,  52,  74);   // hover / pressed
const sf::Color C_BORDER  (44,  60,  88);   // subtle border
const sf::Color C_DIVIDER (30,  42,  62);   // divider line

// Accents
const sf::Color C_CYAN    (0,   212, 170);  // primary teal
const sf::Color C_CYAN2   (0,   170, 136);  // hover teal
const sf::Color C_GREEN   (52,  211, 153);  // success / in-stock
const sf::Color C_RED     (248, 113, 113);  // danger / out-of-stock
const sf::Color C_AMBER   (251, 191,  36);  // warning / low-stock
const sf::Color C_BLUE    (99,  179, 237);  // info
const sf::Color C_PURPLE  (167, 139, 250);  // analytics / charts
const sf::Color C_PINK    (244, 114, 182);  // highlights

// Text
const sf::Color C_TXT     (224, 236, 252);  // primary text
const sf::Color C_TXT2    (130, 152, 184);  // secondary / label text
const sf::Color C_TXT3    (58,  76,  106);  // muted / disabled text

// ============================================================
// ======================== MODELS ============================
// ============================================================

struct Product {
    int    id           = 0;
    string name;
    string category;
    int    stock        = 0;
    float  price        = 0.f;
    int    lowThreshold = 5;

    bool isLow()  const { return stock > 0 && stock <= lowThreshold; }
    bool isOut()  const { return stock <= 0; }
    string statusStr() const {
        if (isOut()) return "Out";
        if (isLow()) return "Low";
        return "OK";
    }
};

struct CartItem {
    int   productID = 0;
    int   qty       = 0;
    float unitPrice = 0.f;
    float subtotal() const { return qty * unitPrice; }
};

struct Transaction {
    int    id          = 0;
    int    cashierID   = 0;
    string cashierName;
    string date;
    float  subtotal    = 0.f;
    float  taxAmt      = 0.f;
    float  total       = 0.f;
    string items;       // "pid:qty:price|pid:qty:price|..."
};

struct User {
    int    id       = 0;
    string name;
    string password;
    string role;    // "admin" | "cashier"
};

struct Supplier {
    int    id       = 0;
    string name;
    string category;
    string contact;
    string email;
};

// ============================================================
// ======================== STORAGE ===========================
// ============================================================
#define MAX_PROD  300
#define MAX_TX    600
#define MAX_USER   50
#define MAX_SUP    80

Product     prods[MAX_PROD];
Transaction txns[MAX_TX];
User        users[MAX_USER];
Supplier    sups[MAX_SUP];

int prodCount = 0, txCount = 0, userCount = 0, supCount = 0;

// O(1) lookup by product ID
unordered_map<int, int> prodMap;   // productID → index

// Session
int    sesUID  = -1;
string sesName = "";
string sesRole = "";

vector<CartItem> cart;
float  TAX_RATE    = 0.14f;
int    nextTxID    = 1001;

// UI state
string statusMsg   = "";
bool   statusErr   = false;
string searchQ     = "";
string catFilter   = "";
int    prodScroll  = 0;
int    txScroll    = 0;
int    supScroll   = 0;
int    userScroll  = 0;

// ============================================================
// ======================== SCREEN ENUM =======================
// ============================================================
enum Screen {
    SCREEN_LOGIN,
    SCREEN_DASHBOARD,
    SCREEN_POS,
    SCREEN_CHECKOUT_CONFIRM,   // NEW
    SCREEN_PRODUCTS,
    SCREEN_ADD_PRODUCT,
    SCREEN_EDIT_PRODUCT,
    SCREEN_INVENTORY,
    SCREEN_TRANSACTIONS,
    SCREEN_SUPPLIERS,
    SCREEN_ADD_SUPPLIER,
    SCREEN_USERS,              // NEW
    SCREEN_ADD_USER,           // NEW
    SCREEN_ANALYTICS,
    SCREEN_SETTINGS
};
Screen cur = SCREEN_LOGIN;
Screen prevScreen = SCREEN_DASHBOARD;  // for back-navigation

// ============================================================
// ======================== HELPERS ===========================
// ============================================================
void setStatus(const string& msg, bool err = false) {
    statusMsg = msg; statusErr = err;
}

string today() {
    time_t t = time(nullptr);
    char b[32]; strftime(b, 32, "%Y-%m-%d", localtime(&t));
    return string(b);
}

string fmtF(float v, int d = 2) {
    ostringstream ss; ss << fixed << setprecision(d) << v; return ss.str();
}

string trunc(const string& s, int n) {
    return (int)s.size() <= n ? s : s.substr(0, n-2) + "..";
}

bool isAdmin() { return sesRole == "admin"; }

// ============================================================
// ==================== BUSINESS LOGIC ========================
// ============================================================
void rebuildMap() {
    prodMap.clear();
    for (int i = 0; i < prodCount; i++) prodMap[prods[i].id] = i;
}

bool reduceStock(int pid, int qty) {
    auto it = prodMap.find(pid);
    if (it == prodMap.end()) return false;
    if (prods[it->second].stock < qty) return false;
    prods[it->second].stock -= qty;
    return true;
}

bool restock(int pid, int qty) {
    auto it = prodMap.find(pid);
    if (it == prodMap.end()) return false;
    prods[it->second].stock += qty;
    return true;
}

bool addToCart(int pid, int qty = 1) {
    auto it = prodMap.find(pid);
    if (it == prodMap.end()) return false;
    auto& p = prods[it->second];
    if (p.isOut()) return false;
    for (auto& ci : cart) {
        if (ci.productID == pid) {
            if (p.stock < ci.qty + qty) return false;
            ci.qty += qty; return true;
        }
    }
    if (p.stock < qty) return false;
    cart.push_back({pid, qty, p.price});
    return true;
}

void removeFromCart(int pid) {
    for (int i = 0; i < (int)cart.size(); i++) {
        if (cart[i].productID == pid) {
            if (--cart[i].qty <= 0) cart.erase(cart.begin() + i);
            return;
        }
    }
}

float cartSub()   { float s=0; for (auto& c:cart) s+=c.subtotal(); return s; }
float cartTax()   { return cartSub() * TAX_RATE; }
float cartTotal() { return cartSub() + cartTax(); }

// Build receipt text (also used for preview)
string buildReceipt(int txid, float sub, float tax, float total, const string& dateStr) {
    ostringstream r;
    r << "========================================\n";
    r << "         ShopFlow POS — Receipt\n";
    r << "========================================\n";
    r << "Receipt #  : " << txid      << "\n";
    r << "Cashier    : " << sesName   << "\n";
    r << "Date       : " << dateStr   << "\n";
    r << "----------------------------------------\n";
    for (auto& ci : cart) {
        auto it = prodMap.find(ci.productID);
        string pn = (it!=prodMap.end()) ? prods[it->second].name : "Product";
        r << left << setw(20) << trunc(pn,20)
          << " x" << ci.qty
          << "  " << fmtF(ci.subtotal()) << " EGP\n";
    }
    r << "----------------------------------------\n";
    r << "Subtotal   : " << fmtF(sub)   << " EGP\n";
    r << "Tax (14%)  : " << fmtF(tax)   << " EGP\n";
    r << "TOTAL      : " << fmtF(total) << " EGP\n";
    r << "========================================\n";
    r << "      Thank you for shopping!\n";
    r << "========================================\n";
    return r.str();
}

bool doCheckout() {
    if (cart.empty()) return false;
    for (auto& ci : cart) {
        auto it = prodMap.find(ci.productID);
        if (it==prodMap.end() || prods[it->second].stock < ci.qty) return false;
    }
    for (auto& ci : cart) reduceStock(ci.productID, ci.qty);

    float sub = cartSub(), tax = cartTax(), tot = cartTotal();
    int tid = nextTxID++;
    Transaction tx;
    tx.id=tid; tx.cashierID=sesUID; tx.cashierName=sesName;
    tx.date=today(); tx.subtotal=sub; tx.taxAmt=tax; tx.total=tot;
    string items;
    for (auto& ci : cart)
        items += to_string(ci.productID)+":"+to_string(ci.qty)+":"+fmtF(ci.unitPrice)+"|";
    tx.items = items;
    if (txCount < MAX_TX) txns[txCount++] = tx;

    // Write receipt file
    string fname = "receipt_" + to_string(tid) + ".txt";
    ofstream rf(fname);
    rf << buildReceipt(tid, sub, tax, tot, today());
    rf.close();

    cart.clear();
    return true;
}

vector<int> searchProds(const string& q, const string& cat) {
    vector<int> res;
    string ql = q;
    transform(ql.begin(), ql.end(), ql.begin(), ::tolower);
    for (int i = 0; i < prodCount; i++) {
        string nl = prods[i].name; transform(nl.begin(),nl.end(),nl.begin(),::tolower);
        string cl = prods[i].category; transform(cl.begin(),cl.end(),cl.begin(),::tolower);
        bool nm = q.empty() || nl.find(ql)!=string::npos || cl.find(ql)!=string::npos;
        bool cm = cat.empty() || prods[i].category == cat;
        if (nm && cm) res.push_back(i);
    }
    return res;
}

vector<string> getCategories() {
    vector<string> cats;
    for (int i = 0; i < prodCount; i++) {
        if (find(cats.begin(),cats.end(),prods[i].category)==cats.end())
            cats.push_back(prods[i].category);
    }
    return cats;
}

float dailySales(const string& d) {
    float t=0;
    for (int i=0;i<txCount;i++) if(txns[i].date==d) t+=txns[i].total;
    return t;
}

int topSellerIdx() {
    unordered_map<int,float> rev;
    for (int i=0;i<txCount;i++) {
        stringstream ss(txns[i].items); string tok;
        while (getline(ss,tok,'|')) {
            if (tok.empty()) continue;
            stringstream ts(tok); string p,q,r;
            getline(ts,p,':'); getline(ts,q,':'); getline(ts,r,':');
            if (!p.empty()&&!q.empty()&&!r.empty())
                rev[stoi(p)] += stoi(q)*stof(r);
        }
    }
    int bid=-1; float bv=0;
    for (auto& kv:rev) if(kv.second>bv){bv=kv.second;bid=kv.first;}
    auto it = prodMap.find(bid);
    return (it!=prodMap.end()) ? it->second : -1;
}

int lowStockCount() {
    int n=0;
    for (int i=0;i<prodCount;i++) if(prods[i].isLow()||prods[i].isOut()) n++;
    return n;
}

float invValue() {
    float v=0;
    for (int i=0;i<prodCount;i++) v+=prods[i].stock*prods[i].price;
    return v;
}

// ============================================================
// ==================== FILE I/O ==============================
// ============================================================
void seedData() {
    // Users
    users[0]={1,"admin",  "admin123","admin"};
    users[1]={2,"cashier","cash123", "cashier"};
    userCount=2;

    // Products
    prods[0] ={101,"Wireless Mouse",    "Electronics", 45, 149.99f,5};
    prods[1] ={102,"USB-C Hub 7-Port",  "Electronics", 30, 249.99f,5};
    prods[2] ={103,"Notebook A5 80pg",  "Stationery",  120, 19.99f,10};
    prods[3] ={104,"Ballpoint Pen x10", "Stationery",  200, 12.50f,20};
    prods[4] ={105,"Coffee Mug 350ml",  "Kitchen",     60,  39.99f,8};
    prods[5] ={106,"LED Desk Lamp",     "Electronics",  3, 199.99f,5};
    prods[6] ={107,"Stapler H-Duty",   "Stationery",    0,  54.99f,5};
    prods[7] ={108,"Water Bottle 1L",  "Kitchen",      75,  29.99f,10};
    prods[8] ={109,"HDMI Cable 2m",    "Electronics",  18,  79.99f,5};
    prods[9] ={110,"Sticky Notes 100", "Stationery",   85,  14.99f,15};
    prods[10]={111,"Mechanical Keyboard","Electronics",  4, 399.99f,5};
    prods[11]={112,"Whiteboard Marker","Stationery",   60,   8.99f,10};
    prodCount=12;
    rebuildMap();

    // Suppliers
    sups[0]={1,"TechMart Supplies","Electronics","+20-100-1234567","techmart@example.com"};
    sups[1]={2,"Office World Co.",  "Stationery", "+20-100-7654321","officeworld@example.com"};
    sups[2]={3,"KitchenPro Egypt", "Kitchen",    "+20-101-9990000","kitchenpro@example.com"};
    supCount=3;
}

void saveData() {
    { ofstream f("products.txt");
      f << prodCount << "\n";
      for (int i=0;i<prodCount;i++)
          f<<prods[i].id<<"\n"<<prods[i].name<<"\n"<<prods[i].category<<"\n"
           <<prods[i].stock<<"\n"<<prods[i].price<<"\n"<<prods[i].lowThreshold<<"\n"; }

    { ofstream f("users.txt");
      f << userCount << "\n";
      for (int i=0;i<userCount;i++)
          f<<users[i].id<<"\n"<<users[i].name<<"\n"<<users[i].password<<"\n"<<users[i].role<<"\n"; }

    { ofstream f("suppliers.txt");
      f << supCount << "\n";
      for (int i=0;i<supCount;i++)
          f<<sups[i].id<<"\n"<<sups[i].name<<"\n"<<sups[i].category<<"\n"
           <<sups[i].contact<<"\n"<<sups[i].email<<"\n"; }

    { ofstream f("transactions.txt");
      f << txCount << "\n";
      for (int i=0;i<txCount;i++)
          f<<txns[i].id<<"\n"<<txns[i].cashierID<<"\n"<<txns[i].cashierName<<"\n"
           <<txns[i].date<<"\n"<<txns[i].subtotal<<"\n"<<txns[i].taxAmt<<"\n"
           <<txns[i].total<<"\n"<<txns[i].items<<"\n"; }
}

void loadData() {
    { ifstream f("products.txt");
      if (!f.is_open()) { seedData(); return; }
      f>>prodCount; f.ignore();
      for (int i=0;i<prodCount;i++){
          f>>prods[i].id; f.ignore();
          getline(f,prods[i].name); getline(f,prods[i].category);
          f>>prods[i].stock>>prods[i].price>>prods[i].lowThreshold; f.ignore();
      } }

    { ifstream f("users.txt");
      if (f.is_open()) {
          f>>userCount; f.ignore();
          for (int i=0;i<userCount;i++){
              f>>users[i].id; f.ignore();
              getline(f,users[i].name); getline(f,users[i].password); getline(f,users[i].role);
          }
      } else { users[0]={1,"admin","admin123","admin"}; users[1]={2,"cashier","cash123","cashier"}; userCount=2; } }

    { ifstream f("suppliers.txt");
      if (f.is_open()) {
          f>>supCount; f.ignore();
          for (int i=0;i<supCount;i++){
              f>>sups[i].id; f.ignore();
              getline(f,sups[i].name); getline(f,sups[i].category);
              getline(f,sups[i].contact); getline(f,sups[i].email);
          }
      } }

    { ifstream f("transactions.txt");
      if (f.is_open()) {
          f>>txCount; f.ignore();
          for (int i=0;i<txCount;i++){
              f>>txns[i].id>>txns[i].cashierID; f.ignore();
              getline(f,txns[i].cashierName); getline(f,txns[i].date);
              f>>txns[i].subtotal>>txns[i].taxAmt>>txns[i].total; f.ignore();
              getline(f,txns[i].items);
              if (txns[i].id >= nextTxID) nextTxID = txns[i].id+1;
          }
      } }

    rebuildMap();
}

// ============================================================
// ====================== UI PRIMITIVES =======================
// (Same signatures as CliniDo — just colours changed)
// ============================================================

void drawRect(sf::RenderWindow& w, float x, float y, float wd, float h,
              sf::Color fill,
              sf::Color outline = sf::Color::Transparent, float thick = 0.f) {
    sf::RectangleShape r({wd, h});
    r.setPosition({x, y});
    r.setFillColor(fill);
    if (outline != sf::Color::Transparent && thick > 0)
    { r.setOutlineColor(outline); r.setOutlineThickness(thick); }
    w.draw(r);
}

void drawText(sf::RenderWindow& w, sf::Font& f, const string& s,
              float x, float y, unsigned sz, sf::Color col,
              bool bold=false, bool center=false) {
    sf::Text t(f, s, sz);
    t.setFillColor(col);
    if (bold)   t.setStyle(sf::Text::Bold);
    if (center) { auto b=t.getLocalBounds(); t.setPosition({x - b.size.x/2.f, y}); }
    else        t.setPosition({x, y});
    w.draw(t);
}

// ---- Button ------------------------------------------------
struct Button {
    float  x,y,w,h;
    string label;
    sf::Color bg, tc;
    bool   hov = false;
    bool contains(float mx, float my) const {
        return mx>=x && mx<=x+w && my>=y && my<=y+h;
    }
};

void drawButton(sf::RenderWindow& win, sf::Font& font, Button& b) {
    sf::Color bg = b.hov
        ? sf::Color(max(0,(int)b.bg.r-30), max(0,(int)b.bg.g-30), max(0,(int)b.bg.b-30))
        : b.bg;
    drawRect(win, b.x, b.y, b.w, b.h, bg);
    // hairline highlight on top edge
    drawRect(win, b.x, b.y, b.w, 1, sf::Color(255,255,255,18));
    drawText(win, font, b.label, b.x+b.w/2.f, b.y+b.h/2.f-9, 14, b.tc, true, true);
}

void drawSmallBtn(sf::RenderWindow& win, sf::Font& font, Button& b) {
    sf::Color bg = b.hov ? C_CARD3 : b.bg;
    drawRect(win, b.x, b.y, b.w, b.h, bg, C_BORDER, 1);
    drawText(win, font, b.label, b.x+b.w/2.f, b.y+b.h/2.f-8, 12, b.tc, false, true);
}

// ---- Badge -------------------------------------------------
void drawBadge(sf::RenderWindow& win, sf::Font& font,
               float x, float y, const string& text, sf::Color bg, sf::Color tc) {
    float tw = text.size()*6.5f + 14.f;
    drawRect(win, x, y, tw, 20, bg);
    drawText(win, font, text, x+tw/2.f, y+3, 10, tc, true, true);
}

// ---- InputField --------------------------------------------
struct InputField {
    float  x,y,w,h;
    string label;
    string value;
    bool   focused=false, isPass=false;
    bool contains(float mx, float my) const {
        return mx>=x && mx<=x+w && my>=y && my<=y+h;
    }
};

InputField* focusedField = nullptr;

void drawInputField(sf::RenderWindow& win, sf::Font& font, InputField& f) {
    drawText(win, font, f.label, f.x, f.y-20, 11, C_TXT2);
    sf::Color bdr = f.focused ? C_CYAN : C_BORDER;
    drawRect(win, f.x, f.y, f.w, f.h, C_CARD2, bdr, f.focused?2.f:1.f);
    if (f.focused) drawRect(win, f.x, f.y, 3, f.h, C_CYAN);
    string disp = f.isPass ? string(f.value.size(),'*') : f.value;
    if (f.focused) disp += "|";
    if (disp.empty() && !f.focused) {
        // placeholder
        drawText(win, font, "...", f.x+12, f.y+12, 13, C_TXT3);
    } else {
        drawText(win, font, disp, f.x+12, f.y+12, 14, C_TXT);
    }
}

void checkFocus(vector<InputField>& fields, float mx, float my) {
    for (auto& f : fields) {
        if (f.contains(mx, my)) { f.focused=true; focusedField=&f; }
        else f.focused=false;
    }
}

void handleText(uint32_t c) {
    if (!focusedField) return;
    if (c==8) { if (!focusedField->value.empty()) focusedField->value.pop_back(); }
    else if (c>=32 && c<128 && focusedField->value.size()<60)
        focusedField->value += (char)c;
}

// ============================================================
// ===================== SHARED LAYOUT ========================
// ============================================================

// Top navbar (52 px tall)
void drawNav(sf::RenderWindow& w, sf::Font& f, const string& title) {
    drawRect(w, 0,0,1280,52, C_NAV);
    drawRect(w, 0,52,1280,1, C_DIVIDER);

    // Brand block in sidebar zone
    drawRect(w, 0,0,200,52, C_SIDEBAR);
    drawRect(w, 200,0,1,52, C_DIVIDER);
    drawText(w, f, "ShopFlow", 18, 10, 22, C_CYAN, true);
    drawText(w, f, "POS", 122, 14, 12, C_TXT3);

    drawText(w, f, title, 218, 16, 16, C_TXT, true);

    // Right: date | user info
    drawText(w, f, today(), 870, 18, 12, C_TXT2);
    string uinfo = sesName + "  [" + sesRole + "]";
    drawText(w, f, uinfo, 980, 18, 12, C_CYAN);

    // Logout button
    drawRect(w, 1190,12, 82,28, C_CARD2, C_BORDER, 1);
    drawText(w, f, "Logout", 1231,19, 12, C_TXT2, false, true);
}

// Left sidebar (200 px wide, from y=53)
void drawSidebar(sf::RenderWindow& w, sf::Font& f, Screen active) {
    drawRect(w, 0,53,200,747, C_SIDEBAR);
    drawRect(w, 200,53,1,747, C_DIVIDER);

    struct Item { const char* icon; const char* label; Screen sc; bool divBefore; };
    Item nav[] = {
        {"  #", "Dashboard",    SCREEN_DASHBOARD,    false},
        {"  +", "POS Cashier",  SCREEN_POS,          false},
        // divider
        {"  B", "Products",     SCREEN_PRODUCTS,     true},
        {"  =", "Inventory",    SCREEN_INVENTORY,    false},
        // divider
        {"  $", "Transactions", SCREEN_TRANSACTIONS, true},
        {"  T", "Suppliers",    SCREEN_SUPPLIERS,    false},
        {"  U", "Users",        SCREEN_USERS,        false},
        // divider
        {"  *", "Analytics",    SCREEN_ANALYTICS,    true},
        {"  S", "Settings",     SCREEN_SETTINGS,     false},
    };
    int n = sizeof(nav)/sizeof(nav[0]);
    float iy = 72.f;

    for (int i=0; i<n; i++) {
        if (nav[i].divBefore) {
            drawRect(w, 14, iy, 172, 1, C_DIVIDER);
            iy += 10;
        }
        bool sel = (nav[i].sc == active);
        if (sel) {
            drawRect(w, 0, iy-2, 200, 38, C_CARD);
            drawRect(w, 0, iy-2, 3,   38, C_CYAN);
        }
        sf::Color tc = sel ? C_CYAN : C_TXT2;
        bool bold     = sel;
        drawText(w, f, nav[i].icon,  10, iy+4, 13, tc, bold);
        drawText(w, f, nav[i].label, 32, iy+4, 13, tc, bold);
        iy += 40.f;
    }

    // Low-stock mini alert at bottom of sidebar
    int ls = lowStockCount();
    if (ls > 0) {
        drawRect(w, 10, 730, 180, 28, sf::Color(60,35,10), C_AMBER, 1);
        drawText(w, f, to_string(ls)+" low-stock item(s)", 100, 737, 11, C_AMBER, false, true);
    }

    drawText(w, f, "v2.0", 10, 763, 10, C_TXT3);
}

// Status toast bar (bottom, only when message set)
void drawToast(sf::RenderWindow& w, sf::Font& f) {
    if (statusMsg.empty()) return;
    sf::Color bg  = statusErr ? sf::Color(70,15,15)  : sf::Color(8,45,30);
    sf::Color bdr = statusErr ? C_RED               : C_GREEN;
    sf::Color tc  = statusErr ? C_RED               : C_GREEN;
    drawRect(w, 200,762,1080,38, bg, bdr, 1);
    drawRect(w, 200,762,  4, 38, bdr);
    drawText(w, f, statusMsg, 640, 772, 13, tc, false, true);
}

// KPI card (top accent bar + label + big value + sub-label)
void kpiCard(sf::RenderWindow& win, sf::Font& f,
             float x, float y, float wd, float h,
             const string& lbl, const string& val,
             const string& sub, sf::Color accent) {
    drawRect(win, x,y,wd,h, C_CARD, C_BORDER, 1);
    drawRect(win, x,y,wd,3, accent);
    drawText(win, f, lbl, x+14,y+14, 11, C_TXT2);
    drawText(win, f, val, x+14,y+32, 20, C_TXT,  true);
    drawText(win, f, sub, x+14,y+58, 10, accent);
}

// Table header row
void tblHeader(sf::RenderWindow& win, sf::Font& f,
               float x, float y, float wd, float h,
               const vector<pair<string,float>>& cols) {
    drawRect(win, x,y,wd,h, C_CARD2);
    drawRect(win, x,y+h,wd,1, C_BORDER);
    for (auto& c : cols)
        drawText(win, f, c.first, x+c.second, y+h/2-8, 11, C_TXT2, true);
}

// Alternating table row background
void tblRow(sf::RenderWindow& win, float x, float y, float wd, float h, int idx) {
    drawRect(win, x, y, wd, h, idx%2==0 ? C_CARD : C_CARD2);
}

// Section heading + underline
void sectionHead(sf::RenderWindow& win, sf::Font& f,
                 float x, float y, const string& title, sf::Color accent=C_CYAN) {
    drawText(win, f, title, x, y, 13, C_TXT, true);
    drawRect(win, x, y+20, (float)(title.size()*8+20), 2, accent);
}

// ============================================================
// =================== FIELD DEFINITIONS ======================
// ============================================================
// (Mirrors CliniDo: each screen owns its fields as globals)

vector<InputField> loginFlds;
void initLogin() {
    loginFlds.clear();
    loginFlds.push_back({440,330,400,44,"Username","",false,false});
    loginFlds.push_back({440,420,400,44,"Password","",false,true});
    focusedField = nullptr;
}

vector<InputField> posSearch;
void initPosSearch() {
    posSearch.clear();
    posSearch.push_back({218,62,430,34,"","",false,false});
    focusedField = nullptr;
}

vector<InputField> prodSearch;
void initProdSearch() {
    prodSearch.clear();
    prodSearch.push_back({218,62,430,34,"","",false,false});
    focusedField = nullptr;
}

vector<InputField> addProdFlds;
void initAddProd() {
    addProdFlds.clear();
    addProdFlds.push_back({218,175,310,42,"Product Name",  "",false,false});
    addProdFlds.push_back({548,175,310,42,"Category",      "",false,false});
    addProdFlds.push_back({218,265,190,42,"Initial Stock", "",false,false});
    addProdFlds.push_back({420,265,210,42,"Unit Price(EGP)","",false,false});
    addProdFlds.push_back({642,265,180,42,"Low Stock Alert","",false,false});
    focusedField = nullptr;
}

int  editIdx = -1;
vector<InputField> editProdFlds;
void initEditProd(int idx) {
    editIdx = idx;
    editProdFlds.clear();
    if (idx<0||idx>=prodCount) return;
    auto& p = prods[idx];
    editProdFlds.push_back({218,175,310,42,"Product Name",    p.name,               false,false});
    editProdFlds.push_back({548,175,310,42,"Category",        p.category,           false,false});
    editProdFlds.push_back({218,265,190,42,"Stock",           to_string(p.stock),   false,false});
    editProdFlds.push_back({420,265,210,42,"Unit Price(EGP)", fmtF(p.price),        false,false});
    editProdFlds.push_back({642,265,180,42,"Low Stock Alert", to_string(p.lowThreshold),false,false});
    focusedField = nullptr;
}

vector<InputField> rstockFlds;
void initRestock() {
    rstockFlds.clear();
    rstockFlds.push_back({218,205,240,42,"Product ID",  "",false,false});
    rstockFlds.push_back({470,205,240,42,"Add Quantity","",false,false});
    focusedField = nullptr;
}

vector<InputField> addSupFlds;
void initAddSup() {
    addSupFlds.clear();
    addSupFlds.push_back({218,175,310,42,"Supplier Name","",false,false});
    addSupFlds.push_back({548,175,310,42,"Category",    "",false,false});
    addSupFlds.push_back({218,265,310,42,"Phone",       "",false,false});
    addSupFlds.push_back({548,265,310,42,"Email",       "",false,false});
    focusedField = nullptr;
}

vector<InputField> addUserFlds;
void initAddUser() {
    addUserFlds.clear();
    addUserFlds.push_back({218,175,310,42,"Username","",false,false});
    addUserFlds.push_back({548,175,310,42,"Password","",false,true});
    // role toggle handled via buttons
    focusedField = nullptr;
}
int addUserRoleChoice = 1;  // 0=admin 1=cashier

vector<InputField> settingsFlds;
void initSettings() {
    settingsFlds.clear();
    settingsFlds.push_back({218,200,280,42,"New Username",   "",false,false});
    settingsFlds.push_back({518,200,280,42,"New Password",   "",false,true});
    settingsFlds.push_back({218,292,280,42,"Tax Rate (0-1)", fmtF(TAX_RATE),false,false});
    focusedField = nullptr;
}

// ============================================================
// ==================== DRAW SCREENS ==========================
// ============================================================

// ---- LOGIN -------------------------------------------------
void drawLogin(sf::RenderWindow& win, sf::Font& f, vector<Button>& btns) {
    win.clear(C_BG);

    // Split layout: left brand panel | right form
    drawRect(win, 0,0,500,800, C_SIDEBAR);
    drawRect(win, 500,0,1,800, C_DIVIDER);

    // Left brand
    drawRect(win, 40,180,60,4, C_CYAN);   // cyan accent bar above brand
    drawText(win, f, "ShopFlow",  40, 200, 44, C_CYAN, true);
    drawText(win, f, "Point of Sale", 40, 256, 18, C_TXT2);

    // Features
    const char* feats[] = {
        "Modern dark dashboard UI",
        "Fast POS checkout + receipts",
        "Real-time inventory alerts",
        "Analytics & sales charts",
        "Multi-user (admin / cashier)"
    };
    for (int i=0;i<5;i++) {
        drawRect(win, 40, 330+i*42+10, 6, 6, C_CYAN);
        drawText(win, f, feats[i], 56, 330+i*42, 13, C_TXT2);
    }

    // Stats panel at bottom of left side
    drawRect(win, 30,620,440,100, C_CARD, C_BORDER,1);
    drawRect(win, 30,620,440,3, C_CYAN);
    drawText(win, f, to_string(prodCount)+" Products",   120,640,16,C_CYAN,true,true);
    drawText(win, f, to_string(txCount)+" Transactions", 340,640,16,C_GREEN,true,true);
    drawText(win, f, "Inv. Value: "+fmtF(invValue())+" EGP", 250,668,11,C_TXT2,false,true);

    // Right: login card
    drawRect(win, 520,100,740,600, C_CARD, C_BORDER,1);
    drawRect(win, 520,100,740,4, C_CYAN);

    drawText(win, f, "Sign In to ShopFlow", 890, 128, 22, C_TXT,  true, true);
    drawText(win, f, "Enter your credentials", 890, 162, 13, C_TXT2, false, true);

    for (auto& fld : loginFlds) drawInputField(win, f, fld);

    btns.clear();
    btns.push_back({440,494,400,46,"Sign In",C_CYAN,C_BG});
    for (auto& b : btns) drawButton(win, f, b);

    drawText(win, f, "Default: admin/admin123  or  cashier/cash123",
             890, 560, 10, C_TXT3, false, true);

    drawToast(win, f);
}

// ---- DASHBOARD ---------------------------------------------
void drawDashboard(sf::RenderWindow& win, sf::Font& f, vector<Button>& btns) {
    win.clear(C_BG);
    drawNav(win, f, "Dashboard");
    drawSidebar(win, f, SCREEN_DASHBOARD);

    btns.clear();
    btns.push_back({1190,12,82,28,"__logout__",C_CARD2,C_TXT2});

    float cx=218, cy=68;

    drawText(win, f, "Welcome back, "+sesName+"!", cx, cy, 18, C_TXT, true);
    drawText(win, f, today(), cx, cy+26, 11, C_TXT2);

    // KPI row
    float ky=cy+50, kw=192, kh=95, kg=12;
    kpiCard(win,f,cx,          ky,kw,kh,"Today's Revenue",  fmtF(dailySales(today()))+" EGP","Sales today",   C_GREEN);
    kpiCard(win,f,cx+(kw+kg),  ky,kw,kh,"Transactions",     to_string(txCount),               "All time",      C_CYAN);
    kpiCard(win,f,cx+2*(kw+kg),ky,kw,kh,"Products",         to_string(prodCount),             "In catalog",    C_BLUE);
    kpiCard(win,f,cx+3*(kw+kg),ky,kw,kh,"Low Stock Alerts", to_string(lowStockCount()),       "Need attention",C_AMBER);
    kpiCard(win,f,cx+4*(kw+kg),ky,kw,kh,"Inventory Value",  fmtF(invValue())+" EGP",          "Total worth",   C_PURPLE);

    // Quick-action buttons row
    float qy = ky+kh+16;
    drawRect(win,cx,qy,1044,44, C_CARD2);
    drawRect(win,cx,qy+44,1044,1, C_BORDER);
    drawText(win,f,"Quick Actions:", cx+12,qy+13,12,C_TXT2,true);

    btns.push_back({cx+160,qy+8,150,28,"Open POS",     C_CYAN,  C_BG});
    btns.push_back({cx+318,qy+8,150,28,"Add Product",  C_BLUE,  C_TXT});
    btns.push_back({cx+476,qy+8,150,28,"Inventory",    C_GREEN, C_BG});
    btns.push_back({cx+634,qy+8,150,28,"Analytics",    C_PURPLE,C_TXT});

    for (auto& b : btns) drawButton(win, f, b);

    // Recent transactions table
    float ty = qy+60;
    sectionHead(win,f,cx,ty,"Recent Transactions");
    ty+=28;

    tblHeader(win,f,cx,ty,1044,32,{
        {"TX#",8},{"Cashier",90},{"Date",240},{"Subtotal",370},
        {"Tax",490},{"Total",590},{"Items",710}
    });
    ty+=33;

    int shown=0;
    for (int i=txCount-1; i>=0 && shown<9; i--,shown++) {
        tblRow(win,cx,ty,1044,28,shown);
        auto& tx=txns[i];
        int ic=0; { stringstream ss(tx.items); string t; while(getline(ss,t,'|')) if(!t.empty())ic++; }
        drawText(win,f,"#"+to_string(tx.id),      cx+8,  ty+7,11,C_CYAN);
        drawText(win,f,trunc(tx.cashierName,14),   cx+90, ty+7,11,C_TXT);
        drawText(win,f,tx.date,                    cx+240,ty+7,11,C_TXT2);
        drawText(win,f,fmtF(tx.subtotal)+" EGP",   cx+370,ty+7,11,C_TXT);
        drawText(win,f,fmtF(tx.taxAmt)+" EGP",     cx+490,ty+7,11,C_AMBER);
        drawText(win,f,fmtF(tx.total)+" EGP",      cx+590,ty+7,11,C_GREEN,true);
        drawText(win,f,to_string(ic)+" items",      cx+710,ty+7,11,C_TXT2);
        ty+=28;
    }
    if (txCount==0)
        drawText(win,f,"No transactions yet — start selling!",cx+400,ty+20,13,C_TXT3,false,true);

    drawToast(win,f);
}

// ---- POS / CASHIER -----------------------------------------
void drawPOS(sf::RenderWindow& win, sf::Font& f, vector<Button>& btns) {
    win.clear(C_BG);
    drawNav(win, f, "POS  —  Cashier");
    drawSidebar(win, f, SCREEN_POS);

    btns.clear();
    btns.push_back({1190,12,82,28,"__logout__",C_CARD2,C_TXT2});

    float cx=218;

    // ---- Search + category bar ----
    drawRect(win,cx,56,1044,48, C_CARD2);
    drawRect(win,cx,104,1044,1, C_BORDER);
    drawText(win,f,"Search:",cx+8,68,11,C_TXT2);
    for (auto& fld:posSearch) drawInputField(win,f,fld);
    if (!posSearch.empty()) searchQ = posSearch[0].value;

    // Category pills (start after search box)
    float px=cx+460, py=66;
    auto cats = getCategories();

    // "All" pill
    {
        bool sel = catFilter.empty();
        sf::Color bg=sel?C_CYAN:C_CARD,tc=sel?C_BG:C_TXT2;
        drawRect(win,px,py,36,24,bg,sel?sf::Color::Transparent:C_BORDER,1);
        drawText(win,f,"All",px+18,py+4,10,tc,false,true);
        btns.push_back({px,py,36,24,"__cat__",bg,tc});
        px+=42;
    }
    for (int ci=0;ci<(int)cats.size()&&ci<7;ci++) {
        bool sel = (catFilter==cats[ci]);
        sf::Color bg=sel?C_CYAN:C_CARD, tc=sel?C_BG:C_TXT2;
        float tw = cats[ci].size()*6.8f+14.f;
        drawRect(win,px,py,tw,24,bg,sel?sf::Color::Transparent:C_BORDER,1);
        drawText(win,f,cats[ci],px+tw/2.f,py+4,10,tc,false,true);
        // Store category index in label for clean parsing
        btns.push_back({px,py,tw,24,"__cat"+to_string(ci)+"__",bg,tc});
        px+=tw+6;
    }

    // ---- LEFT PANEL: product grid (4 cols × 4 rows) ----
    auto filtered = searchProds(searchQ, catFilter);
    int perPage=16, gstartRow=0;
    {
        float gx=cx+2, gy=112, gw=148, gh=98, ggap=4;
        int   cols=4;
        int   startI=prodScroll;

        int cardI=0;
        for (int fi=startI; fi<(int)filtered.size()&&cardI<perPage; fi++,cardI++) {
            int i=filtered[fi];
            int col=cardI%cols, row=cardI/cols;
            float bx=gx+col*(gw+ggap), by=gy+row*(gh+ggap);

            sf::Color cBg = prods[i].isOut() ? sf::Color(38,18,18) :
                            prods[i].isLow() ? sf::Color(38,32,10) : C_CARD;
            sf::Color accent = prods[i].isOut() ? C_RED :
                               prods[i].isLow() ? C_AMBER : C_CYAN;

            drawRect(win,bx,by,gw,gh, cBg, C_BORDER, 1);
            drawRect(win,bx,by,gw,2,  accent);

            drawText(win,f,trunc(prods[i].name,18),    bx+7,by+9,  11,C_TXT,true);
            drawText(win,f,trunc(prods[i].category,18),bx+7,by+26, 9, C_TXT2);
            drawText(win,f,fmtF(prods[i].price)+" EGP",bx+7,by+44, 12,C_GREEN,true);

            string stk="Stock: "+to_string(prods[i].stock);
            sf::Color sc=prods[i].isOut()?C_RED:prods[i].isLow()?C_AMBER:C_TXT2;
            drawText(win,f,stk, bx+7,by+64, 9,sc);

            if (prods[i].isOut()) {
                drawRect(win,bx,by+gh-18,gw,18,sf::Color(80,0,0,210));
                drawText(win,f,"OUT OF STOCK",bx+gw/2.f,by+gh-15,9,C_RED,true,true);
            } else {
                // Invisible click zone
                btns.push_back({bx,by,gw,gh,
                    "__prod"+to_string(prods[i].id)+"__",
                    sf::Color::Transparent,sf::Color::Transparent});
            }
        }

        // Scroll arrows under grid
        float arY=gy+4*(gh+ggap)+2;
        if (prodScroll>0)
            btns.push_back({cx+2,arY,90,26,"< Prev",C_CARD2,C_TXT2});
        if (prodScroll+perPage<(int)filtered.size())
            btns.push_back({cx+100,arY,90,26,"Next >",C_CARD2,C_TXT2});
        drawText(win,f,to_string(filtered.size())+" found",cx+200,arY+6,10,C_TXT3);
    }

    // ---- RIGHT PANEL: Cart (from x=cx+630) ----
    float rx=cx+626, ry=112, rw=416;

    // Cart header
    drawRect(win,rx,ry,rw,42, C_CARD2);
    drawRect(win,rx,ry,rw,3, C_CYAN);
    drawText(win,f,"Cart",           rx+12,ry+12,14,C_TXT,true);
    drawText(win,f,to_string(cart.size())+" item(s)",rx+rw-12,ry+14,11,C_TXT2,false,true);

    // Cart items (scrollable area)
    float cry=ry+45;
    float cartAreaH=360.f;
    drawRect(win,rx,cry,rw,cartAreaH, C_CARD, C_BORDER,1);

    if (cart.empty()) {
        drawText(win,f,"Cart is empty",       rx+rw/2.f,cry+140,13,C_TXT3,false,true);
        drawText(win,f,"Tap products to add", rx+rw/2.f,cry+162,11,C_TXT3,false,true);
    } else {
        float iY=cry+2;
        for (int ci=0; ci<(int)cart.size()&&iY<cry+cartAreaH-4; ci++) {
            bool odd=ci%2==0;
            drawRect(win,rx+1,iY,rw-2,36, odd?C_CARD:C_CARD2);

            auto it=prodMap.find(cart[ci].productID);
            string pn=(it!=prodMap.end())?trunc(prods[it->second].name,17):"?";
            drawText(win,f,pn,             rx+8,  iY+9,11,C_TXT);
            drawText(win,f,"x"+to_string(cart[ci].qty),
                                           rx+200,iY+9,11,C_TXT2);
            drawText(win,f,fmtF(cart[ci].subtotal())+" EGP",
                                           rx+rw-10,iY+9,11,C_GREEN,false,true);

            // − button
            drawRect(win,rx+230,iY+6,24,24, sf::Color(60,18,18));
            drawText(win,f,"-",rx+242,iY+10,13,C_RED,true,true);
            btns.push_back({rx+230,iY+6,24,24,
                "__rm"+to_string(cart[ci].productID)+"__",
                sf::Color::Transparent,sf::Color::Transparent});

            // + button
            drawRect(win,rx+258,iY+6,24,24, sf::Color(8,44,28));
            drawText(win,f,"+",rx+270,iY+10,13,C_GREEN,true,true);
            btns.push_back({rx+258,iY+6,24,24,
                "__add"+to_string(cart[ci].productID)+"__",
                sf::Color::Transparent,sf::Color::Transparent});

            iY+=36;
        }
    }

    // Totals area
    float toty=cry+cartAreaH+4;
    drawRect(win,rx,toty,rw,110, C_CARD2, C_BORDER,1);
    drawRect(win,rx,toty,rw,2, C_BORDER);

    drawText(win,f,"Subtotal:",   rx+12,toty+10,12,C_TXT2);
    drawText(win,f,fmtF(cartSub())+" EGP",  rx+rw-12,toty+10,12,C_TXT,false,true);
    drawText(win,f,"VAT ("+fmtF(TAX_RATE*100.f,0)+"%):", rx+12,toty+30,12,C_TXT2);
    drawText(win,f,fmtF(cartTax())+" EGP",  rx+rw-12,toty+30,12,C_AMBER,false,true);
    drawRect(win,rx+10,toty+50,rw-20,1, C_BORDER);
    drawText(win,f,"TOTAL:",      rx+12,toty+58,14,C_TXT,true);
    drawText(win,f,fmtF(cartTotal())+" EGP",rx+rw-12,toty+60,16,C_GREEN,true,true);

    // Checkout button
    Button ck{rx,toty+84,rw,32,"CHECKOUT",C_CYAN,C_BG};
    btns.push_back(ck);
    drawButton(win,f,ck);

    // Clear cart
    Button cl{rx,toty+118,rw,22,"Clear Cart",C_CARD2,C_RED};
    btns.push_back(cl);
    drawSmallBtn(win,f,cl);

    drawToast(win,f);
}

// ---- CHECKOUT CONFIRMATION (NEW) ---------------------------
void drawCheckoutConfirm(sf::RenderWindow& win, sf::Font& f, vector<Button>& btns) {
    win.clear(C_BG);
    drawNav(win, f, "Checkout Confirmation");
    drawSidebar(win, f, SCREEN_POS);

    btns.clear();
    btns.push_back({1190,12,82,28,"__logout__",C_CARD2,C_TXT2});

    float cx=218, cy=70;

    // Receipt preview card
    drawRect(win,cx,cy,640,620, C_CARD, C_BORDER,1);
    drawRect(win,cx,cy,640,4, C_CYAN);

    drawText(win,f,"Receipt Preview",cx+14,cy+14,14,C_TXT,true);
    drawRect(win,cx,cy+38,640,1, C_BORDER);

    // Items list
    float iy=cy+50;
    drawRect(win,cx,iy,640,28, C_CARD2);
    drawText(win,f,"Product",          cx+10,iy+7,10,C_TXT2,true);
    drawText(win,f,"Qty",              cx+330,iy+7,10,C_TXT2,true);
    drawText(win,f,"Unit",             cx+400,iy+7,10,C_TXT2,true);
    drawText(win,f,"Subtotal",         cx+500,iy+7,10,C_TXT2,true);
    iy+=28;

    for (int ci=0;ci<(int)cart.size();ci++) {
        tblRow(win,cx,iy,640,26,ci);
        auto it=prodMap.find(cart[ci].productID);
        string pn=(it!=prodMap.end())?prods[it->second].name:"?";
        drawText(win,f,trunc(pn,28),     cx+10,iy+6,11,C_TXT);
        drawText(win,f,"x"+to_string(cart[ci].qty), cx+330,iy+6,11,C_TXT2);
        drawText(win,f,fmtF(cart[ci].unitPrice),     cx+400,iy+6,11,C_TXT2);
        drawText(win,f,fmtF(cart[ci].subtotal())+" EGP",cx+500,iy+6,11,C_GREEN);
        iy+=26;
    }

    // Totals box
    drawRect(win,cx,iy+8,640,90, C_CARD2);
    drawRect(win,cx,iy+8,640,2,  C_BORDER);
    drawText(win,f,"Subtotal:",          cx+14,iy+18,12,C_TXT2);
    drawText(win,f,fmtF(cartSub())+" EGP",  cx+620,iy+18,12,C_TXT,false,true);
    drawText(win,f,"VAT ("+fmtF(TAX_RATE*100,0)+"%):",cx+14,iy+38,12,C_TXT2);
    drawText(win,f,fmtF(cartTax())+" EGP",  cx+620,iy+38,12,C_AMBER,false,true);
    drawRect(win,cx+14,iy+58,612,1, C_BORDER);
    drawText(win,f,"TOTAL:",             cx+14,iy+66,15,C_TXT,true);
    drawText(win,f,fmtF(cartTotal())+" EGP",cx+620,iy+68,17,C_GREEN,true,true);

    // Info panel (right of receipt)
    float ix=cx+660, iy2=cy;
    drawRect(win,ix,iy2,384,280, C_CARD2, C_BORDER,1);
    drawRect(win,ix,iy2,384,4, C_GREEN);
    drawText(win,f,"Order Summary",      ix+14,iy2+14,14,C_TXT,true);
    drawText(win,f,"Cashier:",           ix+14,iy2+50,11,C_TXT2);
    drawText(win,f,sesName,              ix+14,iy2+68,13,C_TXT,true);
    drawText(win,f,"Date:",              ix+14,iy2+100,11,C_TXT2);
    drawText(win,f,today(),              ix+14,iy2+118,13,C_TXT);
    drawText(win,f,"Items in cart:",     ix+14,iy2+150,11,C_TXT2);
    drawText(win,f,to_string(cart.size()),ix+14,iy2+168,18,C_CYAN,true);
    drawText(win,f,"Total Payable:",     ix+14,iy2+200,11,C_TXT2);
    drawText(win,f,fmtF(cartTotal())+" EGP",ix+14,iy2+220,20,C_GREEN,true);

    drawText(win,f,"A receipt file will be saved automatically.",
             ix+14,iy2+262,10,C_TXT3);

    // Confirm / cancel buttons
    btns.push_back({ix,iy2+296,384,44,"Confirm & Complete Sale",C_CYAN,C_BG});
    btns.push_back({ix,iy2+348,184,36,"Cancel",C_CARD2,C_TXT2});
    btns.push_back({ix+200,iy2+348,184,36,"Back to POS",C_CARD,C_TXT2});

    for (auto& b:btns) drawButton(win,f,b);
    drawToast(win,f);
}

// ---- PRODUCTS ----------------------------------------------
void drawProducts(sf::RenderWindow& win, sf::Font& f, vector<Button>& btns) {
    win.clear(C_BG);
    drawNav(win, f, "Product Management");
    drawSidebar(win, f, SCREEN_PRODUCTS);

    btns.clear();
    btns.push_back({1190,12,82,28,"__logout__",C_CARD2,C_TXT2});

    float cx=218, ty=56;

    // Toolbar
    drawRect(win,cx,ty,1044,46, C_CARD2);
    drawRect(win,cx,ty+46,1044,1, C_BORDER);
    drawText(win,f,"Search:",cx+8,ty+16,11,C_TXT2);
    for (auto& fld:prodSearch) drawInputField(win,f,fld);
    if (!prodSearch.empty()) searchQ=prodSearch[0].value;
    btns.push_back({cx+860,ty+10,180,28,"+ Add Product",C_CYAN,C_BG});

    ty+=54;

    // Table header
    tblHeader(win,f,cx,ty,1044,32,{
        {"ID",8},{"Name",66},{"Category",286},{"Stock",448},
        {"Price",530},{"Alert",618},{"Status",698},{"Actions",790}
    });
    ty+=33;

    auto filtered=searchProds(searchQ,"");
    int perPage=15;
    for (int fi=prodScroll; fi<(int)filtered.size()&&ty<740; fi++) {
        int i=filtered[fi];
        tblRow(win,cx,ty,1044,28,fi-prodScroll);

        sf::Color sc=prods[i].isOut()?C_RED:prods[i].isLow()?C_AMBER:C_GREEN;
        drawText(win,f,to_string(prods[i].id),       cx+8,  ty+7,11,C_CYAN);
        drawText(win,f,trunc(prods[i].name,22),      cx+66, ty+7,11,C_TXT);
        drawText(win,f,prods[i].category,             cx+286,ty+7,11,C_TXT2);
        drawText(win,f,to_string(prods[i].stock),    cx+448,ty+7,11,sc,true);
        drawText(win,f,fmtF(prods[i].price),         cx+530,ty+7,11,C_TXT);
        drawText(win,f,to_string(prods[i].lowThreshold),cx+618,ty+7,11,C_TXT2);
        drawBadge(win,f,cx+698,ty+5,prods[i].statusStr(),sc,C_BG);

        // Edit button
        Button eb{cx+790,ty+4,60,20,"Edit",C_CARD,C_BLUE};
        btns.push_back(eb);
        // Encode index
        btns.back().label = "__edit"+to_string(i)+"__";
        drawRect(win,eb.x,eb.y,eb.w,eb.h,C_CARD,C_BORDER,1);
        drawText(win,f,"Edit",eb.x+30,eb.y+4,10,C_BLUE,false,true);

        ty+=28;
    }

    // Pagination
    if (prodScroll>0)
        btns.push_back({cx+2,748,80,24,"< Prev",C_CARD2,C_TXT2});
    if (prodScroll+perPage<(int)filtered.size())
        btns.push_back({cx+88,748,80,24,"Next >",C_CARD2,C_TXT2});
    drawText(win,f,to_string(filtered.size())+" products",cx+860,752,10,C_TXT3);

    for (auto& b:btns) drawButton(win,f,b);
    drawToast(win,f);
}

// ---- ADD PRODUCT -------------------------------------------
void drawAddProduct(sf::RenderWindow& win, sf::Font& f, vector<Button>& btns) {
    win.clear(C_BG);
    drawNav(win, f, "Add New Product");
    drawSidebar(win, f, SCREEN_PRODUCTS);

    btns.clear();
    btns.push_back({1190,12,82,28,"__logout__",C_CARD2,C_TXT2});

    float cx=218, cy=72;
    drawRect(win,cx,cy,840,340, C_CARD, C_BORDER,1);
    drawRect(win,cx,cy,840,4, C_CYAN);
    drawText(win,f,"New Product", cx+14,cy+14,15,C_TXT,true);

    for (auto& fld:addProdFlds) drawInputField(win,f,fld);

    btns.push_back({218,328,190,40,"Add Product",C_CYAN,C_BG});
    btns.push_back({420,328,140,40,"Cancel",     C_CARD2,C_TXT2});
    for (auto& b:btns) drawButton(win,f,b);

    // Tip
    drawRect(win,cx,cy+348,840,64, C_CARD2, C_BORDER,1);
    drawRect(win,cx,cy+348,4,64, C_AMBER);
    drawText(win,f,"Tips:",cx+14,cy+358,11,C_AMBER,true);
    drawText(win,f,"Product ID is auto-assigned. Category must match",cx+14,cy+376,11,C_TXT2);
    drawText(win,f,"existing categories (or enter a new one to create it).",cx+14,cy+392,11,C_TXT2);

    drawToast(win,f);
}

// ---- EDIT PRODUCT ------------------------------------------
void drawEditProduct(sf::RenderWindow& win, sf::Font& f, vector<Button>& btns) {
    win.clear(C_BG);
    drawNav(win, f, "Edit Product");
    drawSidebar(win, f, SCREEN_PRODUCTS);

    btns.clear();
    btns.push_back({1190,12,82,28,"__logout__",C_CARD2,C_TXT2});

    float cx=218, cy=72;
    drawRect(win,cx,cy,840,340, C_CARD, C_BORDER,1);
    drawRect(win,cx,cy,840,4, C_BLUE);

    string title="Edit Product";
    if (editIdx>=0&&editIdx<prodCount)
        title="Edit: "+prods[editIdx].name+" (ID "+to_string(prods[editIdx].id)+")";
    drawText(win,f,title, cx+14,cy+14,14,C_TXT,true);

    for (auto& fld:editProdFlds) drawInputField(win,f,fld);

    btns.push_back({218,328,190,40,"Save Changes",C_BLUE,C_TXT});
    btns.push_back({420,328,140,40,"Cancel",      C_CARD2,C_TXT2});
    // Delete button (admin only)
    if (isAdmin()) {
        btns.push_back({580,328,140,40,"Delete",C_CARD2,C_RED});
    }
    for (auto& b:btns) drawButton(win,f,b);

    drawToast(win,f);
}

// ---- INVENTORY ---------------------------------------------
void drawInventory(sf::RenderWindow& win, sf::Font& f, vector<Button>& btns) {
    win.clear(C_BG);
    drawNav(win, f, "Inventory");
    drawSidebar(win, f, SCREEN_INVENTORY);

    btns.clear();
    btns.push_back({1190,12,82,28,"__logout__",C_CARD2,C_TXT2});

    float cx=218, cy=66;

    // KPI row
    kpiCard(win,f,cx,     cy,220,80,"Total Products",to_string(prodCount),"In catalog",   C_CYAN);
    kpiCard(win,f,cx+234, cy,220,80,"Low Stock",     to_string(lowStockCount()),"Alerts", C_AMBER);
    kpiCard(win,f,cx+468, cy,220,80,"Inv. Value",    fmtF(invValue())+" EGP","Worth",     C_GREEN);

    // Restock panel
    float ry=cy+96;
    drawRect(win,cx,ry,660,120, C_CARD, C_BORDER,1);
    drawRect(win,cx,ry,660,4, C_GREEN);
    drawText(win,f,"Restock a Product",cx+14,ry+14,14,C_TXT,true);

    for (auto& fld:rstockFlds) drawInputField(win,f,fld);
    btns.push_back({218,265,160,36,"Restock",C_GREEN,C_BG});
    btns.push_back({390,265,100,36,"Clear",  C_CARD2,C_TXT2});
    for (auto& b:btns) drawButton(win,f,b);

    // Alert list
    float ly=ry+136;
    sectionHead(win,f,cx,ly,"Low Stock & Out of Stock Alerts", C_AMBER);
    ly+=28;

    tblHeader(win,f,cx,ly,1044,30,{
        {"ID",8},{"Name",64},{"Category",278},{"Stock",430},{"Threshold",506},{"Status",590}
    });
    ly+=31;

    bool any=false;
    for (int i=0;i<prodCount&&ly<740;i++) {
        if (!prods[i].isLow()&&!prods[i].isOut()) continue;
        any=true;
        tblRow(win,cx,ly,1044,26,i);
        sf::Color sc=prods[i].isOut()?C_RED:C_AMBER;
        drawText(win,f,to_string(prods[i].id),   cx+8, ly+6,11,C_CYAN);
        drawText(win,f,trunc(prods[i].name,22),  cx+64, ly+6,11,C_TXT);
        drawText(win,f,prods[i].category,         cx+278,ly+6,11,C_TXT2);
        drawText(win,f,to_string(prods[i].stock), cx+430,ly+6,11,sc,true);
        drawText(win,f,to_string(prods[i].lowThreshold),cx+506,ly+6,11,C_TXT2);
        drawBadge(win,f,cx+590,ly+4,prods[i].statusStr(),sc,C_BG);
        ly+=26;
    }
    if (!any)
        drawText(win,f,"All products well-stocked.",cx+300,ly+14,13,C_GREEN,false,true);

    drawToast(win,f);
}

// ---- TRANSACTIONS ------------------------------------------
void drawTransactions(sf::RenderWindow& win, sf::Font& f, vector<Button>& btns) {
    win.clear(C_BG);
    drawNav(win, f, "Transaction History");
    drawSidebar(win, f, SCREEN_TRANSACTIONS);

    btns.clear();
    btns.push_back({1190,12,82,28,"__logout__",C_CARD2,C_TXT2});

    float cx=218, ty=66;

    // Summary KPIs
    float totalRev=0, maxT=0;
    for (int i=0;i<txCount;i++){totalRev+=txns[i].total;if(txns[i].total>maxT)maxT=txns[i].total;}
    float avg=txCount>0?totalRev/txCount:0;

    kpiCard(win,f,cx,      ty,220,80,"Total Revenue",  fmtF(totalRev)+" EGP","All time",C_GREEN);
    kpiCard(win,f,cx+234,  ty,220,80,"Today",          fmtF(dailySales(today()))+" EGP","Today",C_CYAN);
    kpiCard(win,f,cx+468,  ty,220,80,"Avg. Sale",      fmtF(avg)+" EGP","Per transaction",C_BLUE);
    kpiCard(win,f,cx+702,  ty,220,80,"Transactions",   to_string(txCount),"All time",C_PURPLE);

    ty=ty+96;

    tblHeader(win,f,cx,ty,1044,32,{
        {"TX#",8},{"Cashier",96},{"Date",240},{"Subtotal",360},
        {"Tax",470},{"Total",570},{"Items",680}
    });
    ty+=33;

    int shown=0;
    for (int i=txCount-1-txScroll; i>=0&&ty<748; i--,shown++) {
        tblRow(win,cx,ty,1044,28,shown);
        auto& tx=txns[i];
        int ic=0; {stringstream ss(tx.items);string t;while(getline(ss,t,'|'))if(!t.empty())ic++;}
        drawText(win,f,"#"+to_string(tx.id),      cx+8,  ty+8,11,C_CYAN);
        drawText(win,f,trunc(tx.cashierName,16),   cx+96, ty+8,11,C_TXT);
        drawText(win,f,tx.date,                    cx+240,ty+8,11,C_TXT2);
        drawText(win,f,fmtF(tx.subtotal)+" EGP",  cx+360,ty+8,11,C_TXT);
        drawText(win,f,fmtF(tx.taxAmt)+" EGP",    cx+470,ty+8,11,C_AMBER);
        drawText(win,f,fmtF(tx.total)+" EGP",     cx+570,ty+8,11,C_GREEN,true);
        drawText(win,f,to_string(ic)+" items",     cx+680,ty+8,11,C_TXT2);
        ty+=28;
    }

    if (txCount==0)
        drawText(win,f,"No transactions yet.",cx+400,ty+20,13,C_TXT3,false,true);

    if (txScroll>0)
        btns.push_back({cx+2,754,80,24,"< Prev",C_CARD2,C_TXT2});
    if (txScroll+20<txCount)
        btns.push_back({cx+88,754,80,24,"Next >",C_CARD2,C_TXT2});

    for (auto& b:btns) drawButton(win,f,b);
    drawToast(win,f);
}

// ---- SUPPLIERS ---------------------------------------------
void drawSuppliers(sf::RenderWindow& win, sf::Font& f, vector<Button>& btns) {
    win.clear(C_BG);
    drawNav(win, f, "Supplier Management");
    drawSidebar(win, f, SCREEN_SUPPLIERS);

    btns.clear();
    btns.push_back({1190,12,82,28,"__logout__",C_CARD2,C_TXT2});

    float cx=218, ty=62;

    drawRect(win,cx,ty,1044,42, C_CARD2);
    drawRect(win,cx,ty+42,1044,1, C_BORDER);
    drawText(win,f,to_string(supCount)+" suppliers",cx+12,ty+13,12,C_TXT2);
    btns.push_back({cx+860,ty+8,180,26,"+ Add Supplier",C_CYAN,C_BG});
    ty+=50;

    tblHeader(win,f,cx,ty,1044,32,{
        {"ID",8},{"Name",60},{"Category",282},{"Contact",450},{"Email",644}
    });
    ty+=33;

    for (int i=supScroll;i<supCount&&ty<748;i++) {
        tblRow(win,cx,ty,1044,30,i-supScroll);
        drawText(win,f,to_string(sups[i].id),       cx+8,  ty+8,11,C_CYAN);
        drawText(win,f,trunc(sups[i].name,22),       cx+60, ty+8,11,C_TXT);
        drawText(win,f,sups[i].category,              cx+282,ty+8,11,C_TXT2);
        drawText(win,f,sups[i].contact,               cx+450,ty+8,11,C_TXT);
        drawText(win,f,trunc(sups[i].email,24),       cx+644,ty+8,11,C_BLUE);

        // Delete btn (admin only)
        if (isAdmin()) {
            Button db{cx+930,ty+5,90,20,"Delete",C_CARD,C_RED};
            db.label="__delsup"+to_string(i)+"__";
            btns.push_back(db);
            drawRect(win,db.x,db.y,db.w,db.h,C_CARD,C_BORDER,1);
            drawText(win,f,"Delete",db.x+45,db.y+4,10,C_RED,false,true);
        }
        ty+=30;
    }

    if (supCount==0)
        drawText(win,f,"No suppliers yet.",cx+300,ty+20,13,C_TXT3,false,true);

    for (auto& b:btns) drawButton(win,f,b);
    drawToast(win,f);
}

// ---- ADD SUPPLIER ------------------------------------------
void drawAddSupplier(sf::RenderWindow& win, sf::Font& f, vector<Button>& btns) {
    win.clear(C_BG);
    drawNav(win, f, "Add Supplier");
    drawSidebar(win, f, SCREEN_SUPPLIERS);

    btns.clear();
    btns.push_back({1190,12,82,28,"__logout__",C_CARD2,C_TXT2});

    float cx=218, cy=72;
    drawRect(win,cx,cy,840,310, C_CARD, C_BORDER,1);
    drawRect(win,cx,cy,840,4, C_PURPLE);
    drawText(win,f,"New Supplier",cx+14,cy+14,15,C_TXT,true);

    for (auto& fld:addSupFlds) drawInputField(win,f,fld);
    btns.push_back({218,310,190,40,"Add Supplier",C_PURPLE,C_TXT});
    btns.push_back({420,310,140,40,"Cancel",      C_CARD2, C_TXT2});
    for (auto& b:btns) drawButton(win,f,b);
    drawToast(win,f);
}

// ---- USERS (NEW) -------------------------------------------
void drawUsers(sf::RenderWindow& win, sf::Font& f, vector<Button>& btns) {
    win.clear(C_BG);
    drawNav(win, f, "User Management");
    drawSidebar(win, f, SCREEN_USERS);

    btns.clear();
    btns.push_back({1190,12,82,28,"__logout__",C_CARD2,C_TXT2});

    float cx=218, ty=62;

    drawRect(win,cx,ty,1044,42, C_CARD2);
    drawRect(win,cx,ty+42,1044,1, C_BORDER);
    drawText(win,f,to_string(userCount)+" users registered",cx+12,ty+13,12,C_TXT2);

    if (isAdmin())
        btns.push_back({cx+860,ty+8,180,26,"+ Add User",C_CYAN,C_BG});
    else
        drawText(win,f,"Admin only: user management",cx+500,ty+14,11,C_AMBER,false,true);
    ty+=50;

    tblHeader(win,f,cx,ty,1044,32,{
        {"ID",8},{"Username",60},{"Role",280},{"Actions",480}
    });
    ty+=33;

    for (int i=userScroll;i<userCount&&ty<740;i++) {
        tblRow(win,cx,ty,1044,30,i-userScroll);
        drawText(win,f,to_string(users[i].id), cx+8, ty+8,11,C_CYAN);
        drawText(win,f,users[i].name,           cx+60, ty+8,12,C_TXT);
        sf::Color rc=users[i].role=="admin"?C_CYAN:C_BLUE;
        drawBadge(win,f,cx+280,ty+6,users[i].role,rc,C_BG);

        // Delete (admin only, can't delete yourself)
        if (isAdmin() && users[i].id!=sesUID) {
            Button db{cx+480,ty+5,90,20,"Remove",C_CARD,C_RED};
            db.label="__deluser"+to_string(i)+"__";
            btns.push_back(db);
            drawRect(win,db.x,db.y,db.w,db.h,C_CARD,C_BORDER,1);
            drawText(win,f,"Remove",db.x+45,db.y+4,10,C_RED,false,true);
        } else if (users[i].id==sesUID) {
            drawText(win,f,"(you)",cx+480,ty+10,10,C_TXT3);
        }
        ty+=30;
    }

    for (auto& b:btns) drawButton(win,f,b);
    drawToast(win,f);
}

// ---- ADD USER (NEW) ----------------------------------------
void drawAddUser(sf::RenderWindow& win, sf::Font& f, vector<Button>& btns) {
    win.clear(C_BG);
    drawNav(win, f, "Add User");
    drawSidebar(win, f, SCREEN_USERS);

    btns.clear();
    btns.push_back({1190,12,82,28,"__logout__",C_CARD2,C_TXT2});

    float cx=218, cy=72;
    drawRect(win,cx,cy,840,300, C_CARD, C_BORDER,1);
    drawRect(win,cx,cy,840,4, C_CYAN);
    drawText(win,f,"New User Account",cx+14,cy+14,15,C_TXT,true);

    for (auto& fld:addUserFlds) drawInputField(win,f,fld);

    // Role picker
    drawText(win,f,"Role:", 218, 316, 11, C_TXT2);
    sf::Color bg0 = addUserRoleChoice==0?C_CYAN:C_CARD2;
    sf::Color bg1 = addUserRoleChoice==1?C_CYAN:C_CARD2;
    sf::Color tc0 = addUserRoleChoice==0?C_BG:C_TXT2;
    sf::Color tc1 = addUserRoleChoice==1?C_BG:C_TXT2;
    drawRect(win,280,310,90,28,bg0,C_BORDER,1);
    drawText(win,f,"Admin",  325,315,12,tc0,false,true);
    drawRect(win,380,310,90,28,bg1,C_BORDER,1);
    drawText(win,f,"Cashier",425,315,12,tc1,false,true);

    btns.push_back({280,310,90,28,"__role0__",bg0,tc0});
    btns.push_back({380,310,90,28,"__role1__",bg1,tc1});

    btns.push_back({218,358,190,40,"Create User",C_CYAN,C_BG});
    btns.push_back({420,358,140,40,"Cancel",     C_CARD2,C_TXT2});
    for (auto& b:btns) drawButton(win,f,b);
    drawToast(win,f);
}

// ---- ANALYTICS ---------------------------------------------
void drawAnalytics(sf::RenderWindow& win, sf::Font& f, vector<Button>& btns) {
    win.clear(C_BG);
    drawNav(win, f, "Analytics & Reports");
    drawSidebar(win, f, SCREEN_ANALYTICS);

    btns.clear();
    btns.push_back({1190,12,82,28,"__logout__",C_CARD2,C_TXT2});

    float cx=218, cy=66;

    // KPIs
    float totalRev=0, maxT=0;
    for (int i=0;i<txCount;i++){totalRev+=txns[i].total;if(txns[i].total>maxT)maxT=txns[i].total;}
    float avg=txCount>0?totalRev/txCount:0;

    kpiCard(win,f,cx,      cy,220,80,"Total Revenue",  fmtF(totalRev)+" EGP","All time",  C_GREEN);
    kpiCard(win,f,cx+234,  cy,220,80,"Avg Sale",       fmtF(avg)+" EGP",    "Per tx",     C_CYAN);
    kpiCard(win,f,cx+468,  cy,220,80,"Largest Sale",   fmtF(maxT)+" EGP",   "Single tx",  C_PURPLE);
    kpiCard(win,f,cx+702,  cy,220,80,"Low Stock Items",to_string(lowStockCount()),"Alert",C_AMBER);

    // ---- Bar chart: last 12 transaction totals ----
    float chX=cx, chY=cy+96, chW=580, chH=200;
    drawRect(win,chX,chY,chW,chH, C_CARD, C_BORDER,1);
    drawRect(win,chX,chY,chW,4, C_CYAN);
    drawText(win,f,"Recent Transaction Revenue",chX+10,chY+10,12,C_TXT,true);

    int barN=min(txCount,12);
    if (barN>0) {
        float maxV=1;
        for (int i=txCount-barN;i<txCount;i++) if(txns[i].total>maxV) maxV=txns[i].total;
        float bW=(chW-32.f)/barN-4.f;
        for (int bi=0;bi<barN;bi++) {
            int ti=txCount-barN+bi;
            float bH=max(4.f, (txns[ti].total/maxV)*(chH-52.f));
            float bx=chX+16+bi*(bW+4.f);
            float by=chY+chH-bH-24.f;
            // Gradient-ish: brighter at top
            drawRect(win,bx,by,bW,bH, C_CYAN);
            drawRect(win,bx,by,bW,3,  sf::Color(100,255,230));  // highlight top
            drawText(win,f,fmtF(txns[ti].total,0),bx+bW/2.f,by-14,8,C_TXT2,false,true);
            drawText(win,f,"#"+to_string(txns[ti].id),bx+bW/2.f,chY+chH-16,8,C_TXT3,false,true);
        }
    } else {
        drawText(win,f,"No data yet.",chX+chW/2.f,chY+chH/2.f,12,C_TXT3,false,true);
    }

    // ---- Inventory stock bar chart ----
    float iX=cx+596, iY=chY, iW=446, iH=200;
    drawRect(win,iX,iY,iW,iH, C_CARD, C_BORDER,1);
    drawRect(win,iX,iY,iW,4, C_GREEN);
    drawText(win,f,"Product Stock Levels",iX+10,iY+10,12,C_TXT,true);

    int sbN=min(prodCount,9);
    if (sbN>0) {
        float maxSt=1;
        for (int i=0;i<sbN;i++) if(prods[i].stock>maxSt) maxSt=prods[i].stock;
        float sbW=(iW-24.f)/sbN-3.f;
        for (int bi=0;bi<sbN;bi++) {
            float sbH=max(4.f,(prods[bi].stock/maxSt)*(iH-52.f));
            float bx=iX+12+bi*(sbW+3.f);
            float by=iY+iH-sbH-24.f;
            sf::Color bc=prods[bi].isOut()?C_RED:prods[bi].isLow()?C_AMBER:C_GREEN;
            drawRect(win,bx,by,sbW,sbH, bc);
            drawText(win,f,to_string(prods[bi].stock),bx+sbW/2.f,by-13,8,C_TXT2,false,true);
            drawText(win,f,trunc(prods[bi].name,7),   bx+sbW/2.f,iY+iH-16,7,C_TXT3,false,true);
        }
    }

    // ---- Category value breakdown ----
    float catY=chY+chH+18;
    sectionHead(win,f,cx,catY,"Category Inventory Value");
    catY+=28;

    unordered_map<string,float> catVal;
    for (int i=0;i<prodCount;i++) catVal[prods[i].category]+=prods[i].stock*prods[i].price;
    float totV=0; for (auto& kv:catVal) totV+=kv.second;

    sf::Color cols[]={C_CYAN,C_GREEN,C_BLUE,C_PURPLE,C_AMBER,C_PINK};
    int ci=0; float cRx=cx;
    for (auto& kv:catVal) {
        if (cRx+190>cx+1044) break;
        float pct=totV>0?kv.second/totV*100.f:0;
        sf::Color cc=cols[ci%6];
        drawRect(win,cRx,catY,186,72, C_CARD, C_BORDER,1);
        drawRect(win,cRx,catY,186,4, cc);
        drawText(win,f,trunc(kv.first,16),       cRx+10,catY+12,11,C_TXT,true);
        drawText(win,f,fmtF(pct,1)+"%",          cRx+10,catY+32,14,cc,true);
        drawText(win,f,fmtF(kv.second,0)+" EGP", cRx+10,catY+52,10,C_TXT2);
        cRx+=196; ci++;
    }

    // Top seller badge
    int topI=topSellerIdx();
    if (topI>=0) {
        float tx2=cx+800, ty2=catY;
        drawRect(win,tx2,ty2,250,72, C_CARD2, C_AMBER,1);
        drawRect(win,tx2,ty2,250,4, C_AMBER);
        drawText(win,f,"Top Seller",         tx2+10,ty2+10,11,C_AMBER,true);
        drawText(win,f,trunc(prods[topI].name,22),tx2+10,ty2+30,13,C_TXT,true);
        drawText(win,f,"ID: "+to_string(prods[topI].id),tx2+10,ty2+52,10,C_TXT2);
    }

    drawToast(win,f);
}

// ---- SETTINGS ----------------------------------------------
void drawSettings(sf::RenderWindow& win, sf::Font& f, vector<Button>& btns) {
    win.clear(C_BG);
    drawNav(win, f, "Settings");
    drawSidebar(win, f, SCREEN_SETTINGS);

    btns.clear();
    btns.push_back({1190,12,82,28,"__logout__",C_CARD2,C_TXT2});

    float cx=218, cy=72;

    // Account settings card
    drawRect(win,cx,cy,620,320, C_CARD, C_BORDER,1);
    drawRect(win,cx,cy,620,4, C_BLUE);
    drawText(win,f,"Account & System Settings",cx+14,cy+14,14,C_TXT,true);

    for (auto& fld:settingsFlds) drawInputField(win,f,fld);

    btns.push_back({218,350,190,40,"Save",   C_BLUE,C_TXT});
    btns.push_back({420,350,140,40,"Cancel", C_CARD2,C_TXT2});

    // System info panel
    float ix=cx+640, iy=cy;
    drawRect(win,ix,iy,380,320, C_CARD2, C_BORDER,1);
    drawRect(win,ix,iy,380,4, C_PURPLE);
    drawText(win,f,"System Info",      ix+14,iy+14,14,C_TXT,true);
    drawText(win,f,"ShopFlow POS",     ix+14,iy+50,13,C_TXT2);
    drawText(win,f,"Version 2.0",      ix+14,iy+70,13,C_CYAN,true);
    drawText(win,f,"Engine: SFML 3",   ix+14,iy+100,12,C_TXT2);
    drawText(win,f,"Current User:",    ix+14,iy+134,11,C_TXT2);
    drawText(win,f,sesName,            ix+14,iy+154,14,C_CYAN,true);
    drawText(win,f,"Role: "+sesRole,   ix+14,iy+178,12,C_TXT2);
    drawText(win,f,"Tax Rate:",        ix+14,iy+210,11,C_TXT2);
    drawText(win,f,fmtF(TAX_RATE*100,0)+"%",ix+14,iy+230,18,C_AMBER,true);
    drawText(win,f,"Products: "+to_string(prodCount),ix+14,iy+268,12,C_TXT2);
    drawText(win,f,"Transactions: "+to_string(txCount),ix+14,iy+288,12,C_TXT2);

    for (auto& b:btns) drawButton(win,f,b);
    drawToast(win,f);
}

// ============================================================
// ======================== MAIN ==============================
// ============================================================
int main() {
    sf::RenderWindow window(sf::VideoMode({1280,800}), "ShopFlow POS  v2");
    window.setFramerateLimit(60);

    sf::Font font;
    if (!font.openFromFile("arial.ttf")) {
        cout << "ERROR: arial.ttf not found in the same folder.\n";
        return 1;
    }

    loadData();

    // Init all field vectors
    initLogin();
    initPosSearch();
    initProdSearch();
    initRestock();
    initSettings();

    vector<Button> buttons;

    // ----------------------------------------------------------
    // MAIN LOOP  (same structure as CliniDo)
    // ----------------------------------------------------------
    while (window.isOpen()) {

        // Hover update every frame
        sf::Vector2i mp = sf::Mouse::getPosition(window);
        float mx=(float)mp.x, my=(float)mp.y;
        for (auto& b:buttons) b.hov = b.contains(mx,my);

        // Event loop
        while (auto ev = window.pollEvent()) {

            // Close
            if (ev->is<sf::Event::Closed>()) {
                saveData(); window.close();
            }

            // Mouse click
            if (const auto* click = ev->getIf<sf::Event::MouseButtonPressed>()) {
                if (click->button == sf::Mouse::Button::Left) {
                    float cx2=(float)click->position.x;
                    float cy2=(float)click->position.y;

                    // ---- GLOBAL: logout button ----
                    for (auto& b:buttons)
                        if (b.label=="__logout__" && b.contains(cx2,cy2)) {
                            sesUID=-1; sesName=""; sesRole="";
                            cart.clear(); cur=SCREEN_LOGIN;
                            initLogin(); setStatus("Logged out.",false); saveData();
                        }

                    // ---- SIDEBAR navigation (x < 200, y > 53) ----
                    if (cur!=SCREEN_LOGIN && cx2<200 && cy2>53) {
                        // Map click y to nav items — mirrors drawSidebar spacing
                        // Recompute same iy sequence
                        struct SI{Screen sc;bool div;};
                        SI nav2[]={
                            {SCREEN_DASHBOARD,false},{SCREEN_POS,false},
                            {SCREEN_PRODUCTS,true},{SCREEN_INVENTORY,false},
                            {SCREEN_TRANSACTIONS,true},{SCREEN_SUPPLIERS,false},
                            {SCREEN_USERS,false},{SCREEN_ANALYTICS,true},{SCREEN_SETTINGS,false}
                        };
                        float iy2=72.f;
                        for (auto& ni:nav2) {
                            if (ni.div) iy2+=10;
                            if (cy2>=iy2-2 && cy2<=iy2+38) {
                                cur=ni.sc;
                                prodScroll=0; txScroll=0; supScroll=0; userScroll=0;
                                searchQ=""; catFilter="";
                                if (!posSearch.empty())  posSearch[0].value="";
                                if (!prodSearch.empty()) prodSearch[0].value="";
                                focusedField=nullptr;
                                if (cur==SCREEN_INVENTORY)   initRestock();
                                if (cur==SCREEN_SETTINGS)    initSettings();
                                break;
                            }
                            iy2+=40.f;
                        }
                    }

                    // ---- LOGIN ----
                    if (cur==SCREEN_LOGIN) {
                        checkFocus(loginFlds,cx2,cy2);
                        for (auto& b:buttons) {
                            if (!b.contains(cx2,cy2)) continue;
                            if (b.label=="Sign In") {
                                string un=loginFlds[0].value, pw=loginFlds[1].value;
                                bool ok=false;
                                for (int i=0;i<userCount;i++)
                                    if (users[i].name==un && users[i].password==pw)
                                    { sesUID=users[i].id; sesName=users[i].name; sesRole=users[i].role; ok=true; break; }
                                if (ok) { setStatus("Welcome, "+sesName+"!",false); cur=SCREEN_DASHBOARD; }
                                else      setStatus("Invalid credentials.",true);
                            }
                        }
                    }

                    // ---- DASHBOARD ----
                    else if (cur==SCREEN_DASHBOARD) {
                        for (auto& b:buttons) {
                            if (!b.contains(cx2,cy2)) continue;
                            if      (b.label=="Open POS")    { cur=SCREEN_POS; initPosSearch(); }
                            else if (b.label=="Add Product")  { initAddProd(); cur=SCREEN_ADD_PRODUCT; }
                            else if (b.label=="Inventory")    { initRestock(); cur=SCREEN_INVENTORY; }
                            else if (b.label=="Analytics")    { cur=SCREEN_ANALYTICS; }
                        }
                    }

                    // ---- POS ----
                    else if (cur==SCREEN_POS) {
                        checkFocus(posSearch,cx2,cy2);
                        for (auto& b:buttons) {
                            if (!b.contains(cx2,cy2)) continue;
                            string& lbl=b.label;

                            if (lbl=="CHECKOUT") {
                                if (cart.empty()) setStatus("Cart is empty!",true);
                                else { prevScreen=SCREEN_POS; cur=SCREEN_CHECKOUT_CONFIRM; }
                            }
                            else if (lbl=="Clear Cart") { cart.clear(); setStatus("Cart cleared.",false); }
                            // "All" category
                            else if (lbl=="__cat__") { catFilter=""; prodScroll=0; }
                            // Numbered category pills  __cat0__ __cat1__ ...
                            else if (lbl.size()>7 && lbl.rfind("__cat",0)==0 && lbl.back()=='_' && lbl!="__cat__") {
                                string numStr=lbl.substr(5, lbl.size()-7);
                                bool isNum=!numStr.empty() && all_of(numStr.begin(),numStr.end(),::isdigit);
                                if (isNum) {
                                    int ci2=stoi(numStr);
                                    auto cats=getCategories();
                                    if (ci2<(int)cats.size()) { catFilter=cats[ci2]; prodScroll=0; }
                                }
                            }
                            // Product tap  __prodID__
                            else if (lbl.size()>9 && lbl.rfind("__prod",0)==0) {
                                string idStr=lbl.substr(6,lbl.size()-8);
                                if (!idStr.empty() && all_of(idStr.begin(),idStr.end(),::isdigit)) {
                                    int pid=stoi(idStr);
                                    if (addToCart(pid,1)) setStatus("Added to cart.",false);
                                    else setStatus("Insufficient stock.",true);
                                }
                            }
                            // Remove from cart  __rmID__
                            else if (lbl.size()>6 && lbl.rfind("__rm",0)==0) {
                                string idStr=lbl.substr(4,lbl.size()-6);
                                if (!idStr.empty() && all_of(idStr.begin(),idStr.end(),::isdigit))
                                    removeFromCart(stoi(idStr));
                            }
                            // Add qty  __addID__
                            else if (lbl.size()>7 && lbl.rfind("__add",0)==0) {
                                string idStr=lbl.substr(5,lbl.size()-7);
                                if (!idStr.empty() && all_of(idStr.begin(),idStr.end(),::isdigit)) {
                                    int pid=stoi(idStr);
                                    if (!addToCart(pid,1)) setStatus("Insufficient stock.",true);
                                }
                            }
                            else if (lbl=="< Prev") { if (prodScroll>0) prodScroll-=16; }
                            else if (lbl=="Next >")  { prodScroll+=16; }
                        }
                    }

                    // ---- CHECKOUT CONFIRM ----
                    else if (cur==SCREEN_CHECKOUT_CONFIRM) {
                        for (auto& b:buttons) {
                            if (!b.contains(cx2,cy2)) continue;
                            if (b.label=="Confirm & Complete Sale") {
                                if (doCheckout()) {
                                    setStatus("Sale complete! Receipt saved.",false);
                                    saveData();
                                    cur=SCREEN_DASHBOARD;
                                } else {
                                    setStatus("Checkout failed — stock issue.",true);
                                    cur=SCREEN_POS;
                                }
                            }
                            else if (b.label=="Cancel") { cart.clear(); cur=SCREEN_POS; }
                            else if (b.label=="Back to POS") { cur=SCREEN_POS; }
                        }
                    }

                    // ---- PRODUCTS ----
                    else if (cur==SCREEN_PRODUCTS) {
                        checkFocus(prodSearch,cx2,cy2);
                        for (auto& b:buttons) {
                            if (!b.contains(cx2,cy2)) continue;
                            string& lbl=b.label;
                            if (lbl=="+ Add Product")  { initAddProd(); cur=SCREEN_ADD_PRODUCT; }
                            else if (lbl.rfind("__edit",0)==0&&lbl.back()=='_') {
                                string iStr=lbl.substr(6,lbl.size()-8);
                                if (!iStr.empty()&&all_of(iStr.begin(),iStr.end(),::isdigit))
                                { initEditProd(stoi(iStr)); cur=SCREEN_EDIT_PRODUCT; }
                            }
                            else if (lbl=="< Prev")    { if (prodScroll>0) prodScroll-=15; }
                            else if (lbl=="Next >")     { prodScroll+=15; }
                        }
                    }

                    // ---- ADD PRODUCT ----
                    else if (cur==SCREEN_ADD_PRODUCT) {
                        checkFocus(addProdFlds,cx2,cy2);
                        for (auto& b:buttons) {
                            if (!b.contains(cx2,cy2)) continue;
                            if (b.label=="Add Product") {
                                string nm=addProdFlds[0].value, cat=addProdFlds[1].value;
                                int   stk=atoi(addProdFlds[2].value.c_str());
                                float prc=atof(addProdFlds[3].value.c_str());
                                int   thr=atoi(addProdFlds[4].value.c_str());
                                if (nm.empty()||cat.empty()) setStatus("Name and category required.",true);
                                else if (prodCount>=MAX_PROD) setStatus("Product catalog full.",true);
                                else {
                                    int nid=101;
                                    for (int i=0;i<prodCount;i++) if(prods[i].id>=nid) nid=prods[i].id+1;
                                    prods[prodCount++]={nid,nm,cat,stk,prc,thr<1?5:thr};
                                    rebuildMap(); saveData();
                                    setStatus("Product added: "+nm+" (ID "+to_string(nid)+")",false);
                                    initAddProd(); cur=SCREEN_PRODUCTS;
                                }
                            }
                            else if (b.label=="Cancel") { cur=SCREEN_PRODUCTS; }
                        }
                    }

                    // ---- EDIT PRODUCT ----
                    else if (cur==SCREEN_EDIT_PRODUCT) {
                        checkFocus(editProdFlds,cx2,cy2);
                        for (auto& b:buttons) {
                            if (!b.contains(cx2,cy2)) continue;
                            if (b.label=="Save Changes"&&editIdx>=0) {
                                auto& p=prods[editIdx];
                                p.name        =editProdFlds[0].value;
                                p.category    =editProdFlds[1].value;
                                p.stock       =atoi(editProdFlds[2].value.c_str());
                                p.price       =atof(editProdFlds[3].value.c_str());
                                p.lowThreshold=atoi(editProdFlds[4].value.c_str());
                                rebuildMap(); saveData();
                                setStatus("Product updated: "+p.name,false);
                                cur=SCREEN_PRODUCTS;
                            }
                            else if (b.label=="Delete"&&editIdx>=0&&isAdmin()) {
                                string dname=prods[editIdx].name;
                                for (int i=editIdx;i<prodCount-1;i++) prods[i]=prods[i+1];
                                prodCount--;
                                rebuildMap(); saveData();
                                setStatus("Deleted: "+dname,false);
                                cur=SCREEN_PRODUCTS;
                            }
                            else if (b.label=="Cancel") { cur=SCREEN_PRODUCTS; }
                        }
                    }

                    // ---- INVENTORY ----
                    else if (cur==SCREEN_INVENTORY) {
                        checkFocus(rstockFlds,cx2,cy2);
                        for (auto& b:buttons) {
                            if (!b.contains(cx2,cy2)) continue;
                            if (b.label=="Restock") {
                                int pid=atoi(rstockFlds[0].value.c_str());
                                int qty=atoi(rstockFlds[1].value.c_str());
                                if (qty<=0) setStatus("Enter a valid quantity.",true);
                                else if (restock(pid,qty)) {
                                    saveData();
                                    setStatus("Restocked #"+to_string(pid)+" by "+to_string(qty)+" units.",false);
                                    initRestock();
                                } else setStatus("Product ID not found.",true);
                            }
                            else if (b.label=="Clear") initRestock();
                        }
                    }

                    // ---- TRANSACTIONS ----
                    else if (cur==SCREEN_TRANSACTIONS) {
                        for (auto& b:buttons) {
                            if (!b.contains(cx2,cy2)) continue;
                            if (b.label=="< Prev") { if(txScroll>0) txScroll-=20; }
                            else if (b.label=="Next >") txScroll+=20;
                        }
                    }

                    // ---- SUPPLIERS ----
                    else if (cur==SCREEN_SUPPLIERS) {
                        for (auto& b:buttons) {
                            if (!b.contains(cx2,cy2)) continue;
                            string& lbl=b.label;
                            if (lbl=="+ Add Supplier") { initAddSup(); cur=SCREEN_ADD_SUPPLIER; }
                            else if (lbl.rfind("__delsup",0)==0&&lbl.back()=='_'&&isAdmin()) {
                                string iStr=lbl.substr(8,lbl.size()-10);
                                if (!iStr.empty()&&all_of(iStr.begin(),iStr.end(),::isdigit)) {
                                    int si=stoi(iStr);
                                    string sname=sups[si].name;
                                    for (int k=si;k<supCount-1;k++) sups[k]=sups[k+1];
                                    supCount--; saveData();
                                    setStatus("Supplier removed: "+sname,false);
                                }
                            }
                        }
                    }

                    // ---- ADD SUPPLIER ----
                    else if (cur==SCREEN_ADD_SUPPLIER) {
                        checkFocus(addSupFlds,cx2,cy2);
                        for (auto& b:buttons) {
                            if (!b.contains(cx2,cy2)) continue;
                            if (b.label=="Add Supplier") {
                                string nm=addSupFlds[0].value, cat=addSupFlds[1].value;
                                string co=addSupFlds[2].value, em=addSupFlds[3].value;
                                if (nm.empty()) setStatus("Supplier name required.",true);
                                else if (supCount>=MAX_SUP) setStatus("Supplier list full.",true);
                                else {
                                    int nid=supCount+1;
                                    sups[supCount++]={nid,nm,cat,co,em};
                                    saveData(); setStatus("Supplier added: "+nm,false);
                                    cur=SCREEN_SUPPLIERS;
                                }
                            }
                            else if (b.label=="Cancel") { cur=SCREEN_SUPPLIERS; }
                        }
                    }

                    // ---- USERS ----
                    else if (cur==SCREEN_USERS) {
                        for (auto& b:buttons) {
                            if (!b.contains(cx2,cy2)) continue;
                            string& lbl=b.label;
                            if (lbl=="+ Add User"&&isAdmin()) { initAddUser(); cur=SCREEN_ADD_USER; }
                            else if (lbl.rfind("__deluser",0)==0&&lbl.back()=='_'&&isAdmin()) {
                                string iStr=lbl.substr(9,lbl.size()-11);
                                if (!iStr.empty()&&all_of(iStr.begin(),iStr.end(),::isdigit)) {
                                    int ui=stoi(iStr);
                                    if (users[ui].id!=sesUID) {
                                        string uname2=users[ui].name;
                                        for (int k=ui;k<userCount-1;k++) users[k]=users[k+1];
                                        userCount--; saveData();
                                        setStatus("User removed: "+uname2,false);
                                    }
                                }
                            }
                        }
                    }

                    // ---- ADD USER ----
                    else if (cur==SCREEN_ADD_USER) {
                        checkFocus(addUserFlds,cx2,cy2);
                        for (auto& b:buttons) {
                            if (!b.contains(cx2,cy2)) continue;
                            string& lbl=b.label;
                            if (lbl=="__role0__")      addUserRoleChoice=0;
                            else if (lbl=="__role1__") addUserRoleChoice=1;
                            else if (lbl=="Create User"&&isAdmin()) {
                                string un=addUserFlds[0].value, pw=addUserFlds[1].value;
                                if (un.empty()||pw.empty()) setStatus("Username and password required.",true);
                                else if (userCount>=MAX_USER) setStatus("User list full.",true);
                                else {
                                    int nid=userCount+1;
                                    for (int i=0;i<userCount;i++) if(users[i].id>=nid) nid=users[i].id+1;
                                    string role=addUserRoleChoice==0?"admin":"cashier";
                                    users[userCount++]={nid,un,pw,role};
                                    saveData(); setStatus("User created: "+un+" ["+role+"]",false);
                                    cur=SCREEN_USERS;
                                }
                            }
                            else if (lbl=="Cancel") { cur=SCREEN_USERS; }
                        }
                    }

                    // ---- SETTINGS ----
                    else if (cur==SCREEN_SETTINGS) {
                        checkFocus(settingsFlds,cx2,cy2);
                        for (auto& b:buttons) {
                            if (!b.contains(cx2,cy2)) continue;
                            if (b.label=="Save") {
                                for (int i=0;i<userCount;i++) if(users[i].id==sesUID) {
                                    if (!settingsFlds[0].value.empty()) { users[i].name=settingsFlds[0].value; sesName=users[i].name; }
                                    if (!settingsFlds[1].value.empty()) users[i].password=settingsFlds[1].value;
                                    break;
                                }
                                if (!settingsFlds[2].value.empty()) {
                                    float tr=atof(settingsFlds[2].value.c_str());
                                    if (tr>=0&&tr<=1) TAX_RATE=tr;
                                }
                                saveData(); setStatus("Settings saved.",false);
                                initSettings();
                            }
                            else if (b.label=="Cancel") { cur=SCREEN_DASHBOARD; }
                        }
                    }

                } // left click
            } // MouseButtonPressed

            // Text input (same as CliniDo)
            if (const auto* te = ev->getIf<sf::Event::TextEntered>()) {
                uint32_t c=te->unicode;
                // Enter key on login submits
                if (c==13 && cur==SCREEN_LOGIN) {
                    string un=loginFlds[0].value, pw=loginFlds[1].value;
                    bool ok=false;
                    for (int i=0;i<userCount;i++)
                        if (users[i].name==un && users[i].password==pw)
                        { sesUID=users[i].id; sesName=users[i].name; sesRole=users[i].role; ok=true; break; }
                    if (ok) { setStatus("Welcome, "+sesName+"!",false); cur=SCREEN_DASHBOARD; }
                    else      setStatus("Invalid credentials.",true);
                } else {
                    handleText(c);
                }
            }

            // Mouse wheel scroll
            if (const auto* ws = ev->getIf<sf::Event::MouseWheelScrolled>()) {
                if (cur==SCREEN_POS||cur==SCREEN_PRODUCTS) {
                    if (ws->delta<0) prodScroll+=4;
                    else if (ws->delta>0&&prodScroll>=4) prodScroll-=4;
                    else if (ws->delta>0) prodScroll=0;
                }
                if (cur==SCREEN_TRANSACTIONS) {
                    if (ws->delta<0) txScroll+=4;
                    else if (ws->delta>0&&txScroll>=4) txScroll-=4;
                    else if (ws->delta>0) txScroll=0;
                }
                if (cur==SCREEN_SUPPLIERS) {
                    if (ws->delta<0) supScroll++;
                    else if (ws->delta>0&&supScroll>0) supScroll--;
                }
                if (cur==SCREEN_USERS) {
                    if (ws->delta<0) userScroll++;
                    else if (ws->delta>0&&userScroll>0) userScroll--;
                }
            }

        } // pollEvent

        // ---- DRAW CURRENT SCREEN (same dispatch as CliniDo) ----
        switch (cur) {
            case SCREEN_LOGIN:            drawLogin(window,font,buttons);           break;
            case SCREEN_DASHBOARD:        drawDashboard(window,font,buttons);       break;
            case SCREEN_POS:              drawPOS(window,font,buttons);             break;
            case SCREEN_CHECKOUT_CONFIRM: drawCheckoutConfirm(window,font,buttons); break;
            case SCREEN_PRODUCTS:         drawProducts(window,font,buttons);        break;
            case SCREEN_ADD_PRODUCT:      drawAddProduct(window,font,buttons);      break;
            case SCREEN_EDIT_PRODUCT:     drawEditProduct(window,font,buttons);     break;
            case SCREEN_INVENTORY:        drawInventory(window,font,buttons);       break;
            case SCREEN_TRANSACTIONS:     drawTransactions(window,font,buttons);    break;
            case SCREEN_SUPPLIERS:        drawSuppliers(window,font,buttons);       break;
            case SCREEN_ADD_SUPPLIER:     drawAddSupplier(window,font,buttons);     break;
            case SCREEN_USERS:            drawUsers(window,font,buttons);           break;
            case SCREEN_ADD_USER:         drawAddUser(window,font,buttons);         break;
            case SCREEN_ANALYTICS:        drawAnalytics(window,font,buttons);       break;
            case SCREEN_SETTINGS:         drawSettings(window,font,buttons);        break;
        }

        window.display();

    } // main loop

    return 0;
}
