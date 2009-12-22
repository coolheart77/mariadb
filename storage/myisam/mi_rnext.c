/* Copyright (C) 2000-2004 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#include "myisamdef.h"

#include "rt_index.h"

	/*
	   Read next row with the same key as previous read
	   One may have done a write, update or delete of the previous row.
	   NOTE! Even if one changes the previous row, the next read is done
	   based on the position of the last used key!
	*/

int mi_rnext(MI_INFO *info, uchar *buf, int inx)
{
  int error,changed;
  uint flag;
  ICP_RESULT res= 0;
  DBUG_ENTER("mi_rnext");

  if ((inx = _mi_check_index(info,inx)) < 0)
    DBUG_RETURN(my_errno);
  flag=SEARCH_BIGGER;				/* Read next */
  if (info->lastpos == HA_OFFSET_ERROR && info->update & HA_STATE_PREV_FOUND)
    flag=0;					/* Read first */

  if (fast_mi_readinfo(info))
    DBUG_RETURN(my_errno);
  if (info->s->concurrent_insert)
    rw_rdlock(&info->s->key_root_lock[inx]);
  changed=_mi_test_if_changed(info);
  if (!flag)
  {
    switch(info->s->keyinfo[inx].key_alg){
#ifdef HAVE_RTREE_KEYS
    case HA_KEY_ALG_RTREE:
      error=rtree_get_first(info,inx,info->lastkey_length);
      break;
#endif
    case HA_KEY_ALG_BTREE:
    default:
      error=_mi_search_first(info,info->s->keyinfo+inx,
			   info->s->state.key_root[inx]);
      break;
    }
  }
  else
  {
    switch (info->s->keyinfo[inx].key_alg) {
#ifdef HAVE_RTREE_KEYS
    case HA_KEY_ALG_RTREE:
      /*
	Note that rtree doesn't support that the table
	may be changed since last call, so we do need
	to skip rows inserted by other threads like in btree
      */
      error= rtree_get_next(info,inx,info->lastkey_length);
      break;
#endif
    case HA_KEY_ALG_BTREE:
    default:
      if (!changed)
	error= _mi_search_next(info,info->s->keyinfo+inx,info->lastkey,
			       info->lastkey_length,flag,
			       info->s->state.key_root[inx]);
      else
	error= _mi_search(info,info->s->keyinfo+inx,info->lastkey,
			  USE_WHOLE_KEY,flag, info->s->state.key_root[inx]);
    }
  }

  if (!error)
  {
    while ((info->s->concurrent_insert &&
            info->lastpos >= info->state->data_file_length) ||
           (info->index_cond_func &&
           (res= mi_check_index_cond(info, inx, buf)) == ICP_NO_MATCH))
    {
      /* 
         Skip rows that are either inserted by other threads since
         we got a lock or do not match pushed index conditions
      */
      if  ((error=_mi_search_next(info,info->s->keyinfo+inx,
                                  info->lastkey,
                                  info->lastkey_length,
                                  SEARCH_BIGGER,
                                  info->s->state.key_root[inx])))
        break;
    }
    if (!error && res == ICP_OUT_OF_RANGE)
    {
      if (info->s->concurrent_insert)
        rw_unlock(&info->s->key_root_lock[inx]);
      info->lastpos= HA_OFFSET_ERROR;
      DBUG_RETURN(my_errno= HA_ERR_END_OF_FILE);
    }
  }
  
  if (info->s->concurrent_insert)
    rw_unlock(&info->s->key_root_lock[inx]);

	/* Don't clear if database-changed */
  info->update&= (HA_STATE_CHANGED | HA_STATE_ROW_CHANGED);
  info->update|= HA_STATE_NEXT_FOUND;

  if (error)
  {
    if (my_errno == HA_ERR_KEY_NOT_FOUND)
      my_errno=HA_ERR_END_OF_FILE;
  }
  else if (!buf)
  {
    DBUG_RETURN(info->lastpos==HA_OFFSET_ERROR ? my_errno : 0);
  }
  else if (!(*info->read_record)(info,info->lastpos,buf))
  {
    info->update|= HA_STATE_AKTIV;		/* Record is read */
    DBUG_RETURN(0);
  }
  DBUG_PRINT("error",("Got error: %d,  errno: %d",error, my_errno));
  DBUG_RETURN(my_errno);
} /* mi_rnext */
