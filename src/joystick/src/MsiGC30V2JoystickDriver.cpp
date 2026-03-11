#include "../include/MsiGC30V2JoystickDriver.hpp"
#include <iostream>

// Global pointer to route C callbacks back to the C++ instance
static MsiGC30V2JoystickDriver* g_driverInstance = nullptr;

// --- C Callbacks ---
static void on_insertion(struct hidd_connection *conn, hidd_device_instance_t *dev) {
    if (g_driverInstance) g_driverInstance->handleInsertion(dev);
}

static void on_removal(struct hidd_connection *conn, hidd_device_instance_t *dev) {
    if (g_driverInstance) g_driverInstance->handleRemoval(dev);
}

static void on_report(struct hidd_connection *conn, struct hidd_report *report,
                      void *report_data, _uint32 report_len, _uint32 flags, void *user) {
    if (g_driverInstance) g_driverInstance->handleReport(report, report_data, report_len);
}

// --- C++ Class Implementation ---
MsiGC30V2JoystickDriver::MsiGC30V2JoystickDriver(std::shared_ptr<ButtonMappings> mappings) 
    : m_mappings(mappings), m_connection(nullptr), m_isRunning(false) {
    g_driverInstance = this;
}

MsiGC30V2JoystickDriver::~MsiGC30V2JoystickDriver() {
    stop();
    g_driverInstance = nullptr;
}

bool MsiGC30V2JoystickDriver::start() {
    hidd_device_ident_t interest = { 0x0d22, 0x0c33, HIDD_CONNECT_WILDCARD };
    hidd_funcs_t funcs = { _HIDDI_NFUNCS, on_insertion, on_removal, on_report, NULL };
    hidd_connect_parm_t parm = { NULL, HID_VERSION, HIDD_VERSION, 0, 0, 0, 0, HIDD_CONNECT_WAIT };

    parm.funcs = &funcs;
    parm.device_ident = &interest;

    int status = hidd_connect(&parm, &m_connection);
    if (status != EOK) {
        std::cerr << "[HID Driver] hidd_connect failed." << std::endl;
        return false;
    }
    
    m_isRunning = true;
    std::cout << "[HID Driver] Listening for MSI GC30 V2..." << std::endl;
    return true;
}

void MsiGC30V2JoystickDriver::stop() {
    if (m_isRunning && m_connection) {
        hidd_disconnect(m_connection);
        m_connection = nullptr;
        m_isRunning = false;
    }
}

void MsiGC30V2JoystickDriver::handleInsertion(hidd_device_instance_t *dev) {
    std::cout << "[HID Driver] MSI Controller attached." << std::endl;
    struct hidd_collection **cols;
    _uint16 num_col;
    hidd_get_collections(dev, NULL, &cols, &num_col);
    for (int i = 0; i < num_col; i++) {
        tryAttach(dev, cols[i]);
    }
}

void MsiGC30V2JoystickDriver::tryAttach(hidd_device_instance_t *dev, struct hidd_collection *col) {
    struct hidd_report_instance *report_inst;
    struct hidd_report *report;
    struct hidd_collection **subcols;
    _uint16 num_sub;

    for (int idx = 0; idx < 8; idx++) {
        if (hidd_get_report_instance(col, idx, HID_INPUT_REPORT, &report_inst) == EOK) {
            hidd_report_attach(m_connection, dev, report_inst, 0, 0, &report);
        }
    }

    hidd_get_collections(NULL, col, &subcols, &num_sub);
    for (int i = 0; i < num_sub; i++) {
        tryAttach(dev, subcols[i]);
    }
}

void MsiGC30V2JoystickDriver::handleRemoval(hidd_device_instance_t *dev) {
    std::cout << "[HID Driver] MSI Controller removed." << std::endl;
    hidd_reports_detach(m_connection, dev);
}

void MsiGC30V2JoystickDriver::handleReport(struct hidd_report *report, void *report_data, _uint32 report_len) {
    if (report_len < 7 || !m_mappings) return;

    unsigned char *data = (unsigned char *)report_data;
    ControllerState state;
    
    state.buttons = (data[1] << 8) | data[0]; 
    state.dpad    = data[2]; 
    state.lx      = data[3]; 
    state.ly      = data[4]; 
    state.rx      = data[5]; 
    state.ry      = data[6]; 

    m_mappings->updateState(state);
}