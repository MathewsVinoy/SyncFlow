package com.syncflow.file

import android.graphics.Color
import android.view.LayoutInflater
import android.view.ViewGroup
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.ListAdapter
import androidx.recyclerview.widget.RecyclerView
import com.syncflow.databinding.ItemFileActivityBinding
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class FileActivityAdapter : ListAdapter<FileActivity, FileActivityAdapter.ViewHolder>(DiffCallback()) {

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): ViewHolder {
        val binding = ItemFileActivityBinding.inflate(LayoutInflater.from(parent.context), parent, false)
        return ViewHolder(binding)
    }

    override fun onBindViewHolder(holder: ViewHolder, position: Int) {
        holder.bind(getItem(position))
    }

    inner class ViewHolder(private val binding: ItemFileActivityBinding) :
        RecyclerView.ViewHolder(binding.root) {

        fun bind(activity: FileActivity) {
            binding.apply {
                fileNameText.text = activity.fileName
                filePathText.text = activity.filePath
                fileSizeText.text = activity.getSizeFormatted()
                
                statusIcon.text = activity.getStatusIcon()
                typeTag.apply {
                    text = activity.type.uppercase()
                    setBackgroundColor(activity.getTypeColor())
                    setTextColor(Color.WHITE)
                }
                
                statusTag.apply {
                    text = activity.status.uppercase()
                    setBackgroundColor(getStatusColor(activity.status))
                    setTextColor(Color.WHITE)
                }
                
                timestampText.text = formatTime(activity.timestamp)
                
                if (activity.sourceDevice.isNotBlank()) {
                    deviceText.text = "From: ${activity.sourceDevice}"
                    deviceText.visibility = android.view.View.VISIBLE
                } else {
                    deviceText.visibility = android.view.View.GONE
                }
                
                if (activity.errorMessage.isNotBlank()) {
                    errorText.text = activity.errorMessage
                    errorText.visibility = android.view.View.VISIBLE
                } else {
                    errorText.visibility = android.view.View.GONE
                }
            }
        }

        private fun getStatusColor(status: String): Int {
            return when (status) {
                "pending" -> Color.parseColor("#FFC107")
                "in_progress" -> Color.parseColor("#2196F3")
                "completed" -> Color.parseColor("#4CAF50")
                "failed" -> Color.parseColor("#F44336")
                else -> Color.GRAY
            }
        }

        private fun formatTime(timestamp: Long): String {
            val sdf = SimpleDateFormat("HH:mm:ss", Locale.getDefault())
            return sdf.format(Date(timestamp))
        }
    }

    private class DiffCallback : DiffUtil.ItemCallback<FileActivity>() {
        override fun areItemsTheSame(oldItem: FileActivity, newItem: FileActivity) =
            oldItem.id == newItem.id

        override fun areContentsTheSame(oldItem: FileActivity, newItem: FileActivity) =
            oldItem == newItem
    }
}
