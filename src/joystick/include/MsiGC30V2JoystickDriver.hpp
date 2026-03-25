#ifndef MSI_GC30_V2_JOYSTICK_DRIVER_HPP
#define MSI_GC30_V2_JOYSTICK_DRIVER_HPP

#include "ButtonMappings.hpp"
#include <sys/hiddi.h>
#include <sys/hidut.h>
#include <memory>

class MsiGC30V2JoystickDriver {
public:
    MsiGC30V2JoystickDriver(std::shared_ptr<ButtonMappings> mappings);
    ~MsiGC30V2JoystickDriver();

    bool start();
    void stop();

    // These need to be public so the C-callbacks can reach them, 
    // but they shouldn't be called directly by your application.
    void handleInsertion(hidd_device_instance_t *dev);
    void handleRemoval(hidd_device_instance_t *dev);
    void handleReport(struct hidd_report *report, void *report_data, _uint32 report_len);

private:
    void tryAttach(hidd_device_instance_t *dev, struct hidd_collection *col);

    std::shared_ptr<ButtonMappings> m_mappings;
    struct hidd_connection *m_connection;
    bool m_isRunning;
};

#endif // MSI_GC30_V2_JOYSTICK_DRIVER_HPP