#include <stdlib.h>
#include <string.h>
#include <gphoto2.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef ENABLE_NLS
#  include <libintl.h>
#  undef _
#  define _(String) dgettext (PACKAGE, String)
#  ifdef gettext_noop
#      define N_(String) gettext_noop (String)
#  else
#      define N_(String) (String)
#  endif
#else
#  define _(String) (String)
#  define N_(String) (String)
#endif

#include "globals.h"
#include "library.h"
#include "settings.h"
#include "util.h"

int gp_camera_exit (Camera *camera);

int gp_camera_count ()
{
	return(glob_abilities_list->count);
} 

int gp_camera_name (int camera_number, char *camera_name)
{
	if (glob_abilities_list == NULL)
		return (GP_ERROR_MODEL_NOT_FOUND);

        if (camera_number > glob_abilities_list->count)
                return (GP_ERROR_MODEL_NOT_FOUND);

        strcpy (camera_name, 
		glob_abilities_list->abilities[camera_number]->model);
        return (GP_OK);
}

int gp_camera_abilities (int camera_number, CameraAbilities *abilities)
{
        memcpy (abilities, glob_abilities_list->abilities[camera_number],
               sizeof(CameraAbilities));

        if (glob_debug)
                gp_abilities_dump (abilities);

        return (GP_OK);
}


int gp_camera_abilities_by_name (char *camera_name, CameraAbilities *abilities)
{

        int x;

	for (x = 0; x < glob_abilities_list->count; x++) {
                if (strcmp (glob_abilities_list->abilities[x]->model, 
			    camera_name) == 0)
                        return (gp_camera_abilities(x, abilities));
        }

        return (GP_ERROR_MODEL_NOT_FOUND);
}

int gp_camera_new (Camera **camera)
{

        *camera = (Camera*)malloc(sizeof(Camera));
	if (!*camera) 
		return (GP_ERROR_NO_MEMORY);

        /* Initialize the members */
        (*camera)->port       = (CameraPortInfo*)malloc(sizeof(CameraPortInfo));
	if (!(*camera)->port) 
		return (GP_ERROR_NO_MEMORY);
	(*camera)->port->type = GP_PORT_NONE;
	strcpy ((*camera)->port->path, "");
        (*camera)->abilities  = (CameraAbilities*)malloc(sizeof(CameraAbilities));
        (*camera)->functions  = (CameraFunctions*)malloc(sizeof(CameraFunctions));
        (*camera)->library_handle  = NULL;
        (*camera)->camlib_data     = NULL;
        (*camera)->frontend_data   = NULL;
	(*camera)->session    = glob_session_camera++;
        (*camera)->ref_count  = 1;
	if (!(*camera)->abilities || !(*camera)->functions) 
		return (GP_ERROR_NO_MEMORY);

	/* Initialize function pointers to NULL.*/
	memset((*camera)->functions, 0, sizeof (CameraFunctions));

        return(GP_OK);
}

/************************************************************************
 * Part I:								*
 *             Operations on CAMERAS					*
 *									*
 *   - ref                 : Ref the camera				*
 *   - unref               : Unref the camera				*
 *   - free                : Free					*
 *   - init                : Initialize the camera			*
 *   - get_session         : Get the session identifier			*
 *   - get_config          : Get configuration options			*
 *   - set_config          : Set those configuration options		*
 *   - get_summary         : Get information about the camera		*
 *   - get_manual          : Get information about the driver		*
 *   - get_about           : Get information about authors of the driver*
 *   - capture             : Capture an image				*
 *   - capture_preview     : Capture a preview				*
 *									*
 *   - get_result_as_string: Translate a result into a string		*
 ************************************************************************/

int gp_camera_ref (Camera *camera)
{
	if (camera == NULL)
		return (GP_ERROR_BAD_PARAMETERS);

	camera->ref_count += 1;

	return (GP_OK);
}

int gp_camera_unref (Camera *camera)
{
	if (camera == NULL)
		return (GP_ERROR_BAD_PARAMETERS);
		
	camera->ref_count -= 1;

	if (camera->ref_count == 0)
		gp_camera_free(camera);

	return (GP_OK);
}

int gp_camera_free(Camera *camera)
{
        if (camera == NULL)
                return (GP_ERROR_BAD_PARAMETERS);

        gp_camera_exit(camera);
     
        if (camera->port)
                free(camera->port);
        if (camera->abilities) 
                free(camera->abilities);
        if (camera->functions) 
                free(camera->functions);
            
        return (GP_OK);
}

int gp_camera_init (Camera *camera)
{
	int x, i;
        int result;
        gp_port_info info;

	if (camera == NULL) 
		return (GP_ERROR_BAD_PARAMETERS);

	/* Beware of 'Directory Browse'... */
	if (strcmp (camera->model, "Directory Browse") != 0) {

		/* Set the port's path if only the name has been indicated. */
		if ((strcmp (camera->port->path, "") == 0) && 
		    (strcmp (camera->port->name, "") != 0)) {
			for (x = 0; x < gp_port_count_get (); x++) {
				if (gp_port_info_get (x, &info) == GP_OK) {
					if (strcmp (camera->port->name, 
						    info.name) == 0) {
						strcpy (camera->port->path, 
							info.path);
						break;
					}
				}
			}
			if (x < 0) 
				return (x);
			if (x == gp_port_count_get ())
				return (GP_ERROR_BAD_PARAMETERS);
		}
	
		/* If the port hasn't been indicated, try to figure it 	*/
		/* out (USB only). 					*/
		if (strcmp (camera->port->path, "") == 0) {
			gp_frontend_message (NULL, _("Auto-probe for port has "
					     "not yet been implemented! "
					     "Please specify a port!"));
			return (GP_ERROR_BAD_PARAMETERS);
		}
	
	        /* Set the port type from the path in case the 	*/
		/* frontend didn't. 				*/
	        if (camera->port->type == GP_PORT_NONE) {
	                for (x = 0; x < gp_port_count_get (); x++) {
	                        gp_port_info_get (x, &info);
	                        if (strcmp (info.path, camera->port->path) 
				    == 0) {
	                                camera->port->type = info.type;
					break;
				}
	                }
			if (x < 0) 
				return (x);
			if (x == gp_port_count_get ())
				return (GP_ERROR_BAD_PARAMETERS);
	        }
	}
		
	/* If the model hasn't been indicated, try to figure it out 	*/
	/* through probing. 						*/
	if (strcmp (camera->model, "") == 0) {
		gp_frontend_message (NULL, _("Auto-probe for model has not "
				     "yet been implemented! Please specify "
				     "a model!"));
		return (GP_ERROR_BAD_PARAMETERS);
	}

	/* Fill in camera abilities. */
	gp_debug_printf (GP_DEBUG_LOW, "core", 
			 "Looking up abilities for model %s...", camera->model);
	for (x = 0; x < glob_abilities_list->count; x++) {
		if (strcmp (glob_abilities_list->abilities[x]->model, 
			    camera->model) == 0)
			break;
	}
	if (x == glob_abilities_list->count)
		return GP_ERROR_MODEL_NOT_FOUND;
	camera->abilities->file_operations = 
		glob_abilities_list->abilities[x]->file_operations;
	camera->abilities->folder_operations = 
		glob_abilities_list->abilities[x]->folder_operations;
	camera->abilities->config = glob_abilities_list->abilities[x]->config;
	for (i = 0; glob_abilities_list->abilities[x]->capture [i].type != 
	     GP_CAPTURE_NONE; i++) {
		camera->abilities->capture [i].type = 
			glob_abilities_list->abilities[x]->capture [i].type;
		strcpy (camera->abilities->capture [i].name,
			glob_abilities_list->abilities[x]->capture [i].name);
	}
	camera->abilities->capture [i].type = GP_CAPTURE_NONE;

	/* Load the library. */
	gp_debug_printf (GP_DEBUG_LOW, "core", "Loading library %s...", 
			 glob_abilities_list->abilities[x]->library);
	if ((result = load_library(camera)) != GP_OK)
		return (result);

        return (camera->functions->init (camera));
}

int gp_camera_get_session (Camera *camera)
{
        if (camera == NULL)
                return (GP_ERROR_BAD_PARAMETERS);

        return (camera->session);
}

int gp_camera_get_config (Camera *camera, CameraWidget **window)
{
        if (camera == NULL)
                return (GP_ERROR_BAD_PARAMETERS);

        if (camera->functions->config_get == NULL)
                return (GP_ERROR_NOT_SUPPORTED);

        return (camera->functions->config_get (camera, window));
}

int gp_camera_set_config (Camera *camera, CameraWidget *window)
{
        if ((camera == NULL) || (window == NULL))
                return (GP_ERROR_BAD_PARAMETERS);

        if (camera->functions->config_set == NULL)
                return (GP_ERROR_NOT_SUPPORTED);

        return (camera->functions->config_set (camera, window));
}

int gp_camera_get_summary (Camera *camera, CameraText *summary)
{
        if ((camera == NULL) || (summary == NULL))
                return (GP_ERROR_BAD_PARAMETERS);

        if (camera->functions->summary == NULL)
                return (GP_ERROR_NOT_SUPPORTED);

        return (camera->functions->summary (camera, summary));
}

int gp_camera_get_manual (Camera *camera, CameraText *manual)
{
        if ((camera == NULL) || (manual == NULL))
                return (GP_ERROR_BAD_PARAMETERS);

        if (camera->functions->manual == NULL)
                return (GP_ERROR_NOT_SUPPORTED);

        return (camera->functions->manual (camera, manual));
}

int gp_camera_get_about (Camera *camera, CameraText *about)
{
        if ((camera == NULL) || (about == NULL))
                return (GP_ERROR_BAD_PARAMETERS);

        if (camera->functions->about == NULL)
                return (GP_ERROR_NOT_SUPPORTED);

        return (camera->functions->about (camera, about));
}

int gp_camera_capture (Camera *camera, CameraFilePath *path, CameraCaptureSetting *setting)
{
        if ((camera == NULL) || (path == NULL) || (setting == NULL))
                return (GP_ERROR_BAD_PARAMETERS);

        if (camera->functions->capture == NULL)
                return (GP_ERROR_NOT_SUPPORTED);

        return (camera->functions->capture(camera, path, setting));
}

int gp_camera_capture_preview (Camera *camera, CameraFile *file)
{
        if ((camera == NULL) || (file == NULL))
                return (GP_ERROR_BAD_PARAMETERS);

        if (camera->functions->capture_preview == NULL)
                return (GP_ERROR_NOT_SUPPORTED);

        return (camera->functions->capture_preview(camera, file));
}

char *gp_camera_get_result_as_string (Camera *camera, int result)
{

        /* Camlib error? */
        if (result <= -1000 && (camera != NULL)) {
                if (camera->functions->result_as_string == NULL)
                        return _("Error description not available");
                return (camera->functions->result_as_string (camera, result));
        }

        return (gp_result_as_string (result));
}

int gp_camera_exit (Camera *camera)
{
        int ret;

	if (camera == NULL)
		return (GP_ERROR_BAD_PARAMETERS);

	if (camera->functions == NULL)
		return (GP_ERROR);

        if (camera->functions->exit == NULL)
                return (GP_ERROR_NOT_SUPPORTED);

        ret = camera->functions->exit (camera);
        close_library (camera);

        return (ret);
}

/************************************************************************
 * Part II:								*
 *             Operations on FOLDERS					*
 *									*
 *   - list_files  : Get a list of files in this folder			*
 *   - list_folders: Get a list of folders in this folder		*
 *   - delete_all  : Delete all files in this folder			*
 *   - put_file    : Upload a file into this folder			*
 *   - get_config  : Get configuration options of a folder		*
 *   - set_config  : Set those configuration options			*
 ************************************************************************/

int gp_camera_folder_list_files (Camera *camera, char *folder, 
				 CameraList *list)
{
        CameraListEntry t;
        int x, ret, y, z;

        if ((camera == NULL) || (list == NULL) || (folder == NULL))
                return (GP_ERROR_BAD_PARAMETERS);
	
	if (camera->functions->file_list == NULL)
		return (GP_ERROR_NOT_SUPPORTED);

        /* Initialize the folder list to a known state */
        list->count = 0;

        ret = camera->functions->file_list (camera, list, folder);
        if (ret != GP_OK)
                return (ret);

        /* Sort the file list */
        for (x = 0; x < list->count - 1; x++) {
                for (y = x + 1; y < list->count; y++) {
                        z = strcmp (list->entry[x].name, list->entry[y].name);
                        if (z > 0) {
                                memcpy (&t, &list->entry[x], sizeof (t));
                                memcpy (&list->entry[x], &list->entry[y], 
					sizeof (t));
                                memcpy (&list->entry[y], &t, sizeof (t));
                        }
                }
        }

        return (GP_OK);
}

int gp_camera_folder_list_folders (Camera *camera, char* folder, 
				   CameraList *list)
{
        CameraListEntry t;
        int x, ret, y, z;

	if ((camera == NULL) || (list == NULL) || (folder == NULL))
		return (GP_ERROR_BAD_PARAMETERS);
	
	if (camera->functions->folder_list == NULL)
		return (GP_ERROR_NOT_SUPPORTED);
	
	/* Initialize the folder list to a known state */
        list->count = 0;

        ret = camera->functions->folder_list (camera, list, folder);

        if (ret != GP_OK)
                return (ret);

        /* Sort the folder list */
        for (x = 0; x < list->count - 1; x++) {
                for (y = x + 1; y < list->count; y++) {
                        z = strcmp (list->entry[x].name, list->entry[y].name);
                        if (z > 0) {
                                memcpy (&t, &list->entry[x], sizeof (t));
                                memcpy (&list->entry[x], &list->entry[y], 
					sizeof(t));
                                memcpy (&list->entry[y], &t, sizeof (t));
                        }
                }
        }

        return (GP_OK);
}

int gp_camera_folder_delete_all (Camera *camera, char *folder)
{
        if ((camera == NULL) || (folder == NULL))
                return (GP_ERROR_BAD_PARAMETERS);

        if (camera->functions->folder_delete_all == NULL)
                return (GP_ERROR_NOT_SUPPORTED);

        return (camera->functions->folder_delete_all (camera, folder));
}

int gp_camera_folder_put_file (Camera *camera, char *folder, CameraFile *file)
{
        if ((camera == NULL) || (file == NULL) || (folder == NULL))
                return (GP_ERROR_BAD_PARAMETERS);

        if (camera->functions->folder_put_file == NULL)
                return (GP_ERROR_NOT_SUPPORTED);

        return (camera->functions->folder_put_file (camera, file, folder));
}

int gp_camera_folder_get_config (Camera *camera, char *folder, 
				 CameraWidget **window)
{
        if ((camera == NULL) || (folder == NULL))
                return (GP_ERROR_BAD_PARAMETERS);

        if (camera->functions->folder_config_get == NULL)
                return (GP_ERROR_NOT_SUPPORTED);

        return (camera->functions->folder_config_get (camera, window, folder));
}

int gp_camera_folder_set_config (Camera *camera, char *folder, 
				 CameraWidget *window)
{
        if ((camera == NULL) || (window == NULL) || (folder == NULL))
                return (GP_ERROR_BAD_PARAMETERS);

        if (camera->functions->folder_config_set == NULL)
                return (GP_ERROR_NOT_SUPPORTED);

        return (camera->functions->folder_config_set (camera, window, folder));
}

/************************************************************************
 * Part III:								*
 *             Operations on FILES					*
 *									*
 *   - get_info   : Get specific information about a file		*
 *   - set_info   : Set specific parameters of a file			*
 *   - get_file   : Get a file						*
 *   - get_preview: Get the preview of a file				*
 *   - get_config : Get additional configuration options of a file	*
 *   - set_config : Set those additional configuration options		*
 *   - delete     : Delete a file					*
 ************************************************************************/

int gp_camera_file_get_info (Camera *camera, char *folder, char *file, 
			     CameraFileInfo *info)
{
        if ((camera == NULL) || (info == NULL) || 
	    (folder == NULL) || (file == NULL))
                return (GP_ERROR_BAD_PARAMETERS);

        memset(info, 0, sizeof(CameraFileInfo));

	/* If the camera doesn't support file_info_get, we simply get	*/
	/* the preview and the file and look for ourselves...		*/
	if (camera->functions->file_info_get == NULL) {
		CameraFile *cfile;

		cfile = gp_file_new ();

		/* Get the file */
		info->file.fields = GP_FILE_INFO_NONE;
		if (gp_camera_file_get_file (camera, folder, file, cfile) 
		    == GP_OK) {
			info->file.fields |= GP_FILE_INFO_SIZE | 
					     GP_FILE_INFO_TYPE;
			info->file.size = cfile->size;
			strcpy (info->file.type, cfile->type);
		}

		/* Get the preview */
		info->preview.fields = GP_FILE_INFO_NONE;
		if (gp_camera_file_get_preview (camera, folder, file, cfile) 
		    == GP_OK) {
			info->preview.fields |= GP_FILE_INFO_SIZE | 
						GP_FILE_INFO_TYPE;
			info->preview.size = cfile->size;
			strcpy (info->preview.type, cfile->type);
		}

		gp_file_unref (cfile);

		return (GP_OK);
	}

        return (camera->functions->file_info_get (camera, info, folder, file));
}

int gp_camera_file_set_info (Camera *camera, char *folder, char *file, 
			     CameraFileInfo *info)
{
	if ((camera == NULL) || (info == NULL) || 
	    (folder == NULL) || (file == NULL))
	    	return (GP_ERROR_BAD_PARAMETERS);
	
	if (camera->functions->file_info_set == NULL)
		return (GP_ERROR_NOT_SUPPORTED);
	
	return (camera->functions->file_info_set (camera, info, folder, file));
}

int gp_camera_file_get_file (Camera *camera, char *folder, char *file, 
			     CameraFile *camera_file)
{
        if ((camera == NULL) || (file == NULL) || 
	    (folder == NULL) || (camera_file == NULL))
    		return (GP_ERROR_BAD_PARAMETERS);

        if (camera->functions->file_get == NULL)
                return (GP_ERROR_NOT_SUPPORTED);
	
	/* Did we get reasonable foldername/filename? */
	if (strlen (folder) == 0)
		return (GP_ERROR_DIRECTORY_NOT_FOUND);
	if (strlen (file) == 0)
		return (GP_ERROR_FILE_NOT_FOUND);

        gp_file_clean (camera_file);

        return (camera->functions->file_get(camera, camera_file, folder, file));
}

int gp_camera_file_get_preview (Camera *camera, char *folder, char *file, 
				CameraFile *camera_file)
{
	if ((camera == NULL) || (file == NULL) || 
	    (folder == NULL) || (camera_file == NULL))
		return (GP_ERROR_BAD_PARAMETERS);

        if (camera->functions->file_get_preview == NULL)
                return (GP_ERROR_NOT_SUPPORTED);

        gp_file_clean (camera_file);

        return (camera->functions->file_get_preview (camera, camera_file, 
						     folder, file));
}

int gp_camera_file_get_config (Camera *camera, char *folder, char *file, 
			       CameraWidget **window)
{
	if ((camera == NULL) || (folder == NULL) || (file == NULL))
		return (GP_ERROR_BAD_PARAMETERS);

	if (camera->functions->file_config_get == NULL)
		return (GP_ERROR_NOT_SUPPORTED);
		
	return (camera->functions->file_config_get (camera, folder, file, 
						    window));
}

int gp_camera_file_set_config (Camera *camera, char *folder, char *file, 
			       CameraWidget *window)
{
	if ((camera == NULL) || (window == NULL) || 
	    (folder == NULL) || (file == NULL))
		return (GP_ERROR_BAD_PARAMETERS);

	if (camera->functions->file_config_set == NULL)
		return (GP_ERROR_NOT_SUPPORTED);
	
	return (camera->functions->file_config_set (camera, window, folder, 
						    file));
}

int gp_camera_file_delete (Camera *camera, char *folder, char *file)
{
	if ((camera == NULL) || (folder == NULL) || (file == NULL))
		return (GP_ERROR_BAD_PARAMETERS);

        if (camera->functions->file_delete == NULL)
                return (GP_ERROR_NOT_SUPPORTED);
		
        return (camera->functions->file_delete(camera, folder, file));
}

int gp_camera_config (Camera *camera)
{
	if (camera == NULL)
		return (GP_ERROR_BAD_PARAMETERS);
        if (camera->functions->config == NULL)
                return (GP_ERROR_NOT_SUPPORTED);

        return (camera->functions->config(camera));
}

