# How to add new executables to the launch system
### Step 1:
Add the executable bin name to common/include/executables.hpp

### Step 2:
- Need to also make the launch configuration copy the executable over to the pi
- Click the settings icon for the deploy_reload_all launch configuration
- Go to upload tab
- Click "Add..."
- Add your executable
- Click its Remote Path section (three dots button)
- Set pi_target (do this first)
- Change path to /fs/apps
- Click OK
- Click another OK

### Deploy and reload:
Run the deploy_reload_all launch configuration on pi_target and it will copy over and run your new executable!